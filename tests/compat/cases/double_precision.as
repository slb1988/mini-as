int main() {
    double precise = 16777217;
    float narrowed = precise;
    double computed = 40.125 + 1.875;
    return precise > narrowed && computed > 41.9 && computed < 42.1 ? 42 : 0;
}
