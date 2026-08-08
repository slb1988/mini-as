class Box {
    int base = 20;
    int value = base * 2;
    int get() { return value; }
}

int main() {
    Box@ box = Box();
    return box.get();
}
