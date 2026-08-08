int main() {
    uint binary = 0b101010;
    uint octal = 0o52;
    uint decimal = 0d42;
    uint hex = 0x2A;
    int64 wide = 2147483648;
    uint64 huge = 9223372036854775808;
    double real = 1.25e1;
    float single = 2.5f;
    if (binary != 42 || octal != 42 || decimal != 42 || hex != 42) return 1;
    if (huge != 0x8000000000000000) return 2;
    if (real < 12.49 || real > 12.51) return 3;
    if (single != 2.5f) return 4;
    return 42;
}
