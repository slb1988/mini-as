int main() {
    int result = 0;
    switch (2) {
    case 1: result = 1;
    case 2: result = result + 2;
    case 3: result = result + 3;
    default: result = result + 4;
    }
    return result;
}
