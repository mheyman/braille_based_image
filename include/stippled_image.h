#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <execution>
#include <mdspan>
#include <numeric>
#include <vector>

namespace brma
{
    // --- Concept for 2D mdspan over float ---
    template<typename M>
    concept float_2d_span = requires(M m)
    {
        requires std::same_as<typename M::element_type, float>;
        requires M::rank() == 2;
    };

    namespace detail
    {
        // Gaussian integral approximation using erf
        inline auto gauss_small_sigma(std::vector<float> const& xs, float sigma) -> std::vector<float>
        {
            std::vector<float> result;
            result.reserve(xs.size());
            static float const sqrt_half = std::sqrt(0.5f);

            for (float const val : xs)
            {
                float const p1 = std::erf((val - 0.5f) / sigma * sqrt_half);
                float const p2 = std::erf((val + 0.5f) / sigma * sqrt_half);
                result.push_back((p2 - p1) / 2.0f);
            }

            return result;
        }

        // Outer product
        inline auto outer_product(std::vector<float> const& v, std::vector<float> &backing) -> std::mdspan<float, std::dextents<size_t, 2>>
        {
            size_t n = v.size();
            backing.resize(n * n);
            std::mdspan result(backing.data(), n, n);
            for (size_t i = 0; i < n; ++i)
                for (size_t j = 0; j < n; ++j)
                    result[i, j] = v[i] * v[j];
            return result;
        }

        // Energy shift by wrapping LUT
        template <float_2d_span T>
        auto roll_2d(T const &mat, size_t dx, size_t dy, std::vector<float> &backing) -> std::mdspan<float, std::dextents<size_t, 2>>
        {
            // assert layout_right
            static_assert(std::is_same_v<typename T::layout_type, std::layout_right>,
                "mat must be row-major (std::layout_right)");
            // assert contiguous
            assert((&mat[0, 1] == &mat[0, 0] + 1));
            assert((&mat[1, 0] == &mat[0, 0] + mat.extent(1)));

            size_t const n = mat.extent(0);

            // assert square
            assert(n == mat.extent(1));

            backing.resize(n * n);
            std::mdspan const result(backing.data(), n, n);
            dx %= n;
            dy %= n;

            // make a vector of row indices [0,1,2,...,n-1]
            std::vector<size_t> rows(n);
            std::ranges::iota(rows, 0);

            // Parallel for_each over rows
            std::for_each(
                std::execution::par,
                rows.begin(), rows.end(),
                [dx, dy, n, mat_data = &mat[0,0], result_data = backing.data()](size_t dst_row)
                {
                    // find the source row (wrapped)
                    size_t const src_row = (dst_row + n - dx) % n;
                    float const* src_row_data{ mat_data + (src_row * n) };

                    // now shift columns: copy tail then head
                    size_t const cut = n - dy;
                    // tail -> head of result row
                    std::memcpy(
                        result_data + (dst_row * n),
                        src_row_data + cut,
                        dy * sizeof(float)
                    );
                    // head -> remainder of result row
                    std::memcpy(
                        result_data + (dst_row * n) + dy,
                        src_row_data,
                        cut * sizeof(float)
                    );
                }
            );

            return result;
        }
    }


    class stippled_image
    {
        // based on code from https://bartwronski.com/2022/08/31/progressive-image-stippling-and-greedy-blue-noise-importance-sampling/
        // thank you Bart Wronski
        struct data
        {
            size_t size;
            std::vector<uint8_t> backing;
            std::vector<std::tuple<size_t, size_t, float>> samples;
        } data_;
    public:
        explicit stippled_image(
            float_2d_span auto input_img, 
            float percentage = 0.33f,
            float sigma = 0.9f,
            float content_bias = 0.5f,
            bool negate = false) : data_{init_backing(input_img, percentage, sigma, content_bias, negate)}
        {
        }

        /// @brief Gets the stippled image.
        /// @return The stippled image.
        [[nodiscard]] auto stippled() const -> std::mdspan<bool const, std::dextents<size_t, 2>> { return std::mdspan{ reinterpret_cast<bool const*>(data_.backing.data()), data_.size, data_.size }; }

        /// @brief Gets the samples used to create the stippled image.
        /// @return The samples used to create the stippled image.
        [[nodiscard]] auto samples() const -> std::vector<std::tuple<size_t, size_t, float>> { return data_.samples; }
    private:
        static auto init_backing(
            float_2d_span auto input_img, 
            float percentage, 
            float sigma,
            float content_bias, 
            bool negate) -> data
        {
            assert(input_img.extents().rank() == 2 && input_img.extent(0) == input_img.extent(1));
            uint8_t const dot_value = negate ? 0u : 1u;
            uint8_t const background_value = negate ? 1u : 0u;
            data ret;
            ret.size = input_img.extent(0);
            ret.backing.resize(input_img.extent(0) * input_img.extent(0));
            ret.backing.assign(ret.backing.size(), background_value);
            assert(input_img.extent(1) == ret.size && "input_img must be square");

            // ReSharper disable once CppInconsistentNaming
            std::vector<float> half1s(ret.size / 2);
            // ReSharper disable once CppInconsistentNaming
            std::vector<float> half2s(ret.size / 2);
            std::generate(half1s.begin(), half1s.end(), [i = 0.0f]() mutable { return i++; });
            std::generate(half2s.begin(), half2s.end(), [n = ret.size / 2]() mutable
                {
                    return static_cast<float>(n--);
                });

            std::vector<float> wrapped_pattern_xs;
            wrapped_pattern_xs.reserve(ret.size);
            wrapped_pattern_xs.insert(wrapped_pattern_xs.end(), half1s.begin(), half1s.end());
            wrapped_pattern_xs.insert(wrapped_pattern_xs.end(), half2s.begin(), half2s.end());

            std::vector<float> const gvals{ detail::gauss_small_sigma(wrapped_pattern_xs, sigma) };
            std::vector<float> luts_backing;
            std::mdspan<float, std::dextents<size_t, 2>> luts{ detail::outer_product(gvals, luts_backing) };
            luts[0, 0] = std::numeric_limits<float>::infinity();

            // Construct energy_current
            std::vector<float> energy_currents_backing;
            energy_currents_backing.resize(ret.size * ret.size);
            std::mdspan energy_currents(energy_currents_backing.data(), ret.size, ret.size);
            float sign = negate ? -1.0f : 1.0f;
            for (size_t i = 0; i < ret.size; ++i)
                for (size_t j = 0; j < ret.size; ++j)
                    energy_currents[i, j] = input_img[i, j] * content_bias * sign;

            std::mdspan final_stipple(ret.backing.data(), ret.size, ret.size);

            const size_t sample_count {static_cast<size_t>(static_cast<float>(ret.size * ret.size) * percentage)};

            // ReSharper disable once CppTooWideScope
            std::vector<float> rolled_backing;
            for (size_t iter = 0; iter < sample_count; ++iter)
            {
                // Find minimum position. This uses the knowledge that
                // energy_currents and rolled have a backing array that is
                // contiguous and std::layout_right.
                ptrdiff_t pos{ std::distance(energy_currents_backing.begin(), std::ranges::min_element(energy_currents_backing)) };
                size_t min_x = static_cast<size_t>(pos / energy_currents.extent(0));
                size_t min_y = static_cast<size_t>(pos % energy_currents.extent(0));

                // Update energy with rolled LUT
                (void)detail::roll_2d(luts, min_x, min_y, rolled_backing);
                std::transform(energy_currents_backing.begin(), energy_currents_backing.end(), rolled_backing.begin(), energy_currents_backing.begin(),
                    [](float a, float b) { return a + b; });

                ret.samples.emplace_back(std::tuple{ min_x, min_y, input_img[min_x, min_y] });

                final_stipple[min_x, min_y] = dot_value;
            }

            return ret;
        }
    };
}
