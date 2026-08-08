int main() {
    int8 signedByte = 127;
    signedByte++;
    uint8 unsignedByte = 255;
    unsignedByte++;

    int16 a = 1;
    uint16 b = 1;
    int32 c = 1;
    uint32 d = 1;
    int64 e = 1;
    uint64 f = 1;
    int64 sum = a + b + c + d + e + f;

    return signedByte == -128 && unsignedByte == 0 ? sum + 36 : 0;
}
