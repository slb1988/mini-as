int main() {
    array<int>@ values = {20, 21, 1};
    array<int>@ empty = {};
    return int(values.length() + empty.length()) + 39;
}
