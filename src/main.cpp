// ReSharper disable CppInconsistentNaming
#define cimg_use_png
#define cimg_use_lapack
// ReSharper restore CppInconsistentNaming
#include <CImg.h>
#include <iostream>
#include <vector>
#include <mdspan>
#include <string>
#include <braille_based_image.h>
#include <stippled_image.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN  // NOLINT(clang-diagnostic-unused-macros)
#define NOMINMAX  // NOLINT(clang-diagnostic-unused-macros)
#include <clocale>
#include <Windows.h>
#endif


namespace
{
    auto mask()-> void
    {

        std::array<bool, 48> flat_mask = { {
            true, true, false, false, true, true,
            false, true, true, true, true, false,
            false, false, true, true, false, false,
            false, false, true, true, false, false,
            false, false, true, true, false, false,
            false, true, true, true, true, false,
            true, true, false, false, true, true,
            true, true, true, true, true, true,
        } };

        constexpr std::size_t rows = 8;
        constexpr std::size_t cols = 6;
        auto const mask = std::mdspan(flat_mask.data(), rows, cols);
        std::cout << brma::mask_braille(mask, brma::border::line) << "\n";
        std::cout << brma::mask_braille(mask) << "\n";
    }

    auto image() -> void
    {
        constexpr int image_size {256};

        // Load and convert to grayscale
        cimg_library::CImg<float> image("obama.png");
        if (image.is_empty())
        {
            std::cerr << "Failed to load image.\n";
            return;
        }
        if (image.spectrum() == 3)
        {
            image = image.get_RGBtoYCbCr().get_channel(0); // Convert to grayscale
        }
        image.resize(image_size, image_size); // Resize with default cubic interpolation
        image.blur(0.5f);  // sharpening makes the stippled image better

        // Normalize to [0, 1]
        float const image_min = image.min();
        float const image_max = image.max();
        image = (image - image_min) / (image_max - image_min);

        // Flatten into std::vector<float>
        std::vector<float> image_data(image.size());
        std::ranges::copy(image, image_data.begin());

        // Wrap in mdspan
        std::mdspan<float, std::extents<size_t, image_size, image_size>> const image_mdspan(image_data.data());

        // Example access
        brma::stippled_image const stippled_image(image_mdspan, .5);
        std::cout << brma::mask_braille(stippled_image.stippled(), brma::border::line) << "\n";
    }
}


int main()
{
#ifdef _WIN32
    (void)std::setlocale(LC_CTYPE, ".UTF8");  // NOLINT(concurrency-mt-unsafe)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    mask();
    image();
    return 0;
}
