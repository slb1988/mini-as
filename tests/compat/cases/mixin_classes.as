mixin class Reusable {
    int value = 40;
    int read() { return value + classContribution(); }
    int selected() { return 1; }
}

class Concrete : Reusable {
    int classContribution() { return 2; }
    int selected() { return 42; }
}

int main() {
    Concrete@ value = Concrete();
    return value.read() == 42 && value.selected() == 42 ? 42 : 0;
}
