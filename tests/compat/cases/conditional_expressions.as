int zero = 0;

int main() {
    int value = false ? 1 : true ? 42 : 1 / zero;
    return value;
}
