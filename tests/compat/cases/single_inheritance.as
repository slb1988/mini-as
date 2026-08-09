class Base {
    int value;

    Base() { value = 40; }
    int score() { return value; }
}

class Derived : Base {
    int score() { return value + 2; }
}

int main() {
    Base@ item = Derived();
    return item.score();
}
