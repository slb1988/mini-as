int main() {
    int total = 0;
    for (int i = 1; i <= 3; i = i + 1) {
        if (i == 2) continue;
        total = total + i;
    }
    int w = 0;
    while (w < 3) {
        w = w + 1;
        if (w == 2) continue;
        total = total + 10;
    }
    int d = 0;
    do {
        d = d + 1;
        if (d < 2) continue;
        total = total + 100;
    } while (d < 2);
    return total;
}
