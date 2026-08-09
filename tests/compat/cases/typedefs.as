typedef int Score;

Score bonus = 2;

Score add(Score value) {
    Score result = value + bonus;
    return result;
}

int main() {
    return add(40);
}
