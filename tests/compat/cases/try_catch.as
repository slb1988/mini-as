int divide(int value) {
    return 100 / value;
}

int main() {
    int result = 1;
    try {
        result = divide(0);
        result = 99;
    } catch {
        result = 40;
    }
    try {
        try {
            result = divide(result - 40);
        } catch {
            result += 2;
        }
    } catch {
        result = -1;
    }
    try {
        result += 1;
    } catch {
        result = -2;
    }
    return result;
}
