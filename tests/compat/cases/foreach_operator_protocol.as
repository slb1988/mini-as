int main() {
    array<int>@ values = {19, 99, 21};
    int total = 0;
    foreach (auto value, auto index : values) {
        if (index == 1) continue;
        total += value + int(index);
    }
    int seen = 0;
    foreach (auto value : values) {
        seen++;
        if (seen == 2) break;
    }
    return total + seen - 2;
}
