class Box {
    int value;
    Box(int first) { value = first; }
    Box(int first, int second) { value = first + second; }
    int get() { return value; }
}

int main() {
    Box@ large = Box(40);
    Box@ small = Box(1, 1);
    return large.get() + small.get();
}
