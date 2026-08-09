void adjust(int &out doubled, int &in value, int &inout total) {
    doubled = value * 2;
    total += doubled;
}

int main() {
    int doubled;
    int total = 2;
    adjust(doubled, 20, total);
    return doubled + total - 40;
}
