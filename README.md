# braille_based_image
Convert mdspan into braille-based image for viewing on console

Like:
```c++
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

    auto const mask = std::mdspan(flat_mask.data(), 8, 6);
    std::cout << brma::mask_braille(mask, brma::border::line) << "\n";
    std::cout << brma::mask_braille(mask) << "\n";
    // ╭───╮
    // │⠙⣶⠋│
    // │⣴⣛⣦│
    // ╰───╯
    // ⠙⣶⠋
    // ⣴⣛⣦

```
