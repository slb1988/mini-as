int main() {
    auto integer = 40;
    auto floating = integer + 0.5;
    const auto delta = 2;
    if (floating > 40.0) return integer + delta;
    return 0;
}
