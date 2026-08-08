int global = 3;

int main() {
    int value = 4;
    value += 2;
    global *= 2;
    value %= 5;
    return value + global;
}
