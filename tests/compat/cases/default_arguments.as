namespace Config {
    int bonus = 2;

    int add(int value, int delta = bonus) {
        return value + delta;
    }
}

class Box {
    int value;

    Box(int initial = 40) {
        value = initial;
    }

    int add(int delta = 2) {
        return value + delta;
    }
}

int main() {
    Box@ box = Box();
    return Config::add(40) + box.add() - 42;
}
