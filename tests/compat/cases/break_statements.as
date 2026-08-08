int main() {
    int total = 0;
    for (int i = 0; i < 5; i = i + 1) {
        switch (i) {
        case 2: break;
        default: total = total + 1; break;
        }
        total = total + 10;
        if (i == 3) break;
    }
    return total;
}
