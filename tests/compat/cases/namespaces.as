namespace Root {
    int base = 2;

    namespace Left {
        int value = 40;

        int read() {
            return value + base;
        }
    }

    namespace Right {
        int value = 1;
    }

    class Box {
        int value = 40;

        int read() {
            return value;
        }
    }

    enum Delta {
        Bonus = 2
    }
}

int main() {
    Root::Box@ box = Root::Box();
    return Root::Left::read() + Root::Right::value - 1 + box.read() + Root::Bonus - 42;
}
