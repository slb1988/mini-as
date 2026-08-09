funcdef int Unary(int);

class Accumulator {
    int total;

    Accumulator(int value) {
        total = value;
    }

    int add(int value) {
        total += value;
        return total;
    }
}

int main() {
    Accumulator@ object = Accumulator(10);
    Unary@ callback = Unary(object.add);
    return callback(12) + callback(8);
}
