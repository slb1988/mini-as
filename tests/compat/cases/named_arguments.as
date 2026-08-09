int combine(int first, int second = 0, int third = 0) {
    return first + second + third;
}

class Box {
    int combine(int first, int second = 0) {
        return first + second;
    }
}

int main() {
    Box@ box = Box();
    return combine(third: 2, first: 38) +
           box.combine(second: 2, first: 38) - 38;
}
