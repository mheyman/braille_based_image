#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <mdspan>
#include <numeric>
#include <ranges>
#include <string>
#include <utf8cpp/utf8.h>
#include <vector>

namespace brma
{
    /// @brief The type must be a 2D mdspan of bool.
    template <typename T>
    concept bool_2d_mdspan =
        requires {
        typename T::element_type;
        typename T::extents_type;
    } &&
        (std::same_as<typename T::element_type, bool> ||
            std::same_as<typename T::element_type, const bool>) &&
        (T::extents_type::rank() == 2);

    namespace detail
    {
        template <typename T>
        concept is_2d_mdspan =
            requires {
            typename T::element_type;
            typename T::extents_type;
        }&&
            requires(T t)
        {
            { t.extent(0) } -> std::convertible_to<std::size_t>;
            { t.extent(1) } -> std::convertible_to<std::size_t>;
        }&&
            T::extents_type::rank() == 2;

        template<typename T>
        concept braille_mdspan_c =
            std::same_as<typename T::element_type, const uint32_t>&&
            requires {
            typename T::accessor_type;
            requires requires (typename T::accessor_type acc)
            {
                { acc(std::size_t{}, std::size_t{}) } -> std::convertible_to<uint32_t>;
            };
        };

        /// @brief Accessor that adds virtual padding to the original 2D mdspan of bool.
        /// @tparam T 
        template <bool_2d_mdspan T>
        struct padded_bool_accessor
        {
            T base;
            int pad_top;
            int pad_left;

            using element_type = bool const;
            using reference = bool const;
            using data_handle_type = bool const*;
            using offset_policy = padded_bool_accessor;


            constexpr padded_bool_accessor(T const& base, size_t pt, size_t pl)
                : base{ base }, pad_top{ static_cast<int>(pt) }, pad_left{ static_cast<int>(pl) }
            {}


            constexpr reference access(data_handle_type p, std::size_t i, std::size_t j) const
            {
                int const orig_i = static_cast<int>(i) - pad_top;
                int const orig_j = static_cast<int>(j) - pad_left;
                return (orig_i >= 0 && orig_i < static_cast<int>(base.extent(0)) &&
                    orig_j >= 0 && orig_j < static_cast<int>(base.extent(1)) &&
                    base[orig_i, orig_j]);
            }

            constexpr reference access(data_handle_type p, std::size_t n) const
            {
                return (*this)(n / base.extent(1), n % base.extent(1));
            }

            constexpr reference operator()(data_handle_type p, std::size_t i, std::size_t j) const
            {
                return access(p, i, j);
            }

            constexpr reference operator()(std::size_t i, std::size_t j) const
            {
                return (*this)(nullptr, i, j);
            }

            constexpr data_handle_type offset(data_handle_type p, std::size_t) const noexcept
            {
                (void)this;
                return p;
            }
        };

        /// @brief Accessor that transforms a 2D mdspan of bool into Braille code points
        /// @tparam BoolSpan The type that holds a 2D mdspan of bool.
        template <bool_2d_mdspan BoolSpan>
        struct braille_accessor
        {
            BoolSpan padded_view;

            using element_type = uint32_t const;
            using reference = uint32_t const;
            using data_handle_type = std::nullptr_t;
            using offset_policy = braille_accessor;

            constexpr reference access(data_handle_type, std::size_t row, std::size_t col) const
            {
                constexpr std::array<std::tuple<uint8_t, size_t, size_t>, 8> dots = { {
                    {0x01, 0, 0}, {0x08, 0, 1},
                    {0x02, 1, 0}, {0x10, 1, 1},
                    {0x04, 2, 0}, {0x20, 2, 1},
                    {0x40, 3, 0}, {0x80, 3, 1},
                } };

                return std::accumulate(dots.begin(), dots.end(), 0x2800U,
                    [row, col, *this](uint32_t acc, auto const& dot)
                    {
                        auto [value, di, dj] = dot;
                        return acc + (value * padded_view[(row * 4) + di, (col * 2) + dj]);
                    });
            }

            [[nodiscard]] constexpr reference access(data_handle_type, std::size_t n) const
            {
                return (*this)(n / (padded_view.extent(1) / 2), n % (padded_view.extent(1) / 2));
            }

            constexpr reference operator()(data_handle_type p, std::size_t i, std::size_t j) const
            {
                return access(p, i, j);
            }

            constexpr reference operator()(std::size_t i, std::size_t j) const
            {
                return (*this)(nullptr, i, j);
            }

            [[nodiscard]] constexpr data_handle_type offset(data_handle_type p, std::size_t) const noexcept
            {
                (void)this;
                return p;
            }
        };

        /// @brief Iterates a row in a 2D mdspan.
        /// @tparam T 
        template <is_2d_mdspan T>
        class row_iterator
        {
            const T* view_;
            std::size_t row_;
            std::size_t col_;
        public:
            using value_type = char32_t;
            using reference = char32_t const&;
            using pointer = const char32_t*;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::input_iterator_tag;

            row_iterator(const T* view, std::size_t row, std::size_t col)
                : view_(view), row_(row), col_(col)
            {}
            row_iterator() : view_(nullptr), row_(0), col_(0) {}

            value_type operator*() const
            {
                return static_cast<value_type>((*view_)[row_, col_]);
            }

            row_iterator& operator++() { ++col_; return *this; }
            row_iterator operator++(int)
            {
                auto tmp = *this;
                ++(*this);
                return tmp;
            }

            friend bool operator==(const row_iterator& a, const row_iterator& b)
            {
                return a.col_ == b.col_ && a.row_ == b.row_ && a.view_ == b.view_;
            }

            friend bool operator!=(const row_iterator& a, const row_iterator& b)
            {
                return !(a == b);
            }
        };


        template <is_2d_mdspan T>
        auto row_range(T const& view, std::size_t row)
        {
            return std::ranges::subrange<
                row_iterator<T>,
                row_iterator<T>,
                std::ranges::subrange_kind::sized
            >{
                row_iterator(&view, row, 0),
                    row_iterator(&view, row, view.extent(1)),
                    view.extent(1)
            };

        }

        template <braille_mdspan_c BrailleView>
        auto get_framed_braille_image_lines(BrailleView const& braille_view) -> std::vector<std::string>
        {
            std::vector<std::string> ret;
            std::string bar{
                fmt::format(
                    "{}",
                    fmt::join(std::views::iota(static_cast<size_t>(0), braille_view.extent(1))
                              | std::views::transform([](size_t) -> std::string
                              {
                                  return { "─" };
                              }),
                              "")) };
            ret.emplace_back(fmt::format("╭{}╮", bar));
            ret.append_range(std::views::iota(static_cast<size_t>(0), braille_view.extent(0))
                | std::views::transform([braille_view](size_t i)
                    {
                        auto row = row_range(braille_view, i);
                        std::string framed_ret;
                        utf8::utf32to8(row.begin(), row.end(), std::back_inserter(framed_ret));
                        return fmt::format("│{}│", framed_ret);
                    }));
            ret.emplace_back(fmt::format("╰{}╯", bar));
            return ret;
        }

        template <braille_mdspan_c BrailleView>
        auto get_braille_image_lines(BrailleView const& braille_view) -> std::vector<std::string>
        {
            return std::views::iota(static_cast<size_t>(0), braille_view.extent(0))
                | std::views::transform([braille_view](size_t i) -> std::string
                    {
                        auto row = row_range(braille_view, i);
                        std::string ret;
                        utf8::utf32to8(row.begin(), row.end(), std::back_inserter(ret));
                        return ret;
                    })
                | std::ranges::to<std::vector>();
        }
    }

    /// Tell the mask_braille method to include a border or not.
    enum class border : uint8_t
    {
        none,
        line,
    };

    /// Given a 2D mdspan of bool, return UTF8-encoded Braille text that has
    /// pips for every true value. Optionally includes a UTF8-encoded border.
    /// @param mask The mask to convert into a Braille-text based view of the
    ///     mask. This is any 2D mdspan of bool.
    /// @param border Whether to include a border.
    /// @return A Braille-text version of the given mask.
    template <bool_2d_mdspan MaskSpan>
    std::string mask_braille(MaskSpan const& mask, brma::border border = border::none)
    {
        size_t const mask_height = static_cast<int>(mask.extent(0));
        size_t const mask_width = static_cast<int>(mask.extent(1));

        size_t const required_pad_height = ((4 - (mask_height % 4)) % 4);
        size_t const required_pad_width = (2 - (mask_width % 2)) % 2;
        size_t const required_pad_top = required_pad_height / 2;
        size_t const required_pad_left = required_pad_width / 2;

        size_t const full_height = mask_height + required_pad_height;
        size_t const full_width = mask_width + required_pad_width;

        // define the padded mdspan
        using padded_mdspan_t = std::mdspan<
            const bool,
            std::extents<std::size_t, std::dynamic_extent, std::dynamic_extent>,
            std::layout_right,
            detail::padded_bool_accessor<MaskSpan>
        >;

        // Construct padded_view
        padded_mdspan_t padded_view{
            mask.data_handle(),
            std::extents{full_height, full_width},
            detail::padded_bool_accessor<MaskSpan>{ mask, required_pad_top, required_pad_left }
        };

        // Build Braille mdspan
        using braille_mdspan_t = std::mdspan<
            const uint32_t,
            std::extents<std::size_t, std::dynamic_extent, std::dynamic_extent>,
            std::layout_right,
            detail::braille_accessor<padded_mdspan_t>
        >;


        braille_mdspan_t const braille_view{
            nullptr, // no backing storage needed
            std::extents{full_height / 4, full_width / 2},
            detail::braille_accessor{padded_view}
        };

        return fmt::format("{}", fmt::join(border == brma::border::line ? detail::get_framed_braille_image_lines(braille_view) : detail::get_braille_image_lines(braille_view), "\n"));
    }
}
