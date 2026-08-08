int main() {
    uint value = 0x0F;
    value |= 0x20;
    value ^= 0x01;
    value &= 0x2E;
    value <<= 1;
    value >>= 1;

    int negative = -8;
    int arithmetic = negative >>> 1;
    int logical = negative >> 1;
    int precedence = 1 | 2 ^ 3 & 1;
    return value == 46 && ~0 == -1 && arithmetic == -4 &&
           logical == 2147483644 && precedence == 3 ? 42 : 0;
}
