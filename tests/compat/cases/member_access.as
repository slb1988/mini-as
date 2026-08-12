class Base {
    private int secret = 1;
    protected int value;

    protected Base(int input) { value = input; }
    private int hidden() { return secret; }
    protected int score() { return value + hidden(); }
    int read() { return score(); }
}

class Derived : Base {
    Derived() { super(40); }
    int bump() { value += 2; return score(); }
}

int main() {
    Derived@ item = Derived();
    return item.read() + item.bump();
}
