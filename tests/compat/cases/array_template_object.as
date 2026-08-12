int main() {
    array<int>@ values = array<int>(2);
    values.resize(1);
    values.insertLast(42);
    values.removeLast();
    return int(values.length()) + 41;
}
