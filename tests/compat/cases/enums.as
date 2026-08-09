enum Color {
    Red = 2,
    Green,
    Blue = Green + 2
}

Color selected = Blue;

int main() {
    Color local = Green;
    switch (local) {
    case Red:
        return 0;
    case Green:
        return selected == Blue ? 42 : 0;
    default:
        return 0;
    }
}
