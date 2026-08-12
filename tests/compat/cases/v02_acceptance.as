int shared = 0;

interface IScore {
    int score();
}

class Accumulator : IScore {
    int value = 0;

    Accumulator(int seed) {
        value = seed;
    }

    int add(int amount) {
        value += amount;
        return value;
    }

    int score() {
        return value;
    }
}

int main() {
    Accumulator@ accumulator = Accumulator(0);

    for (int i = 0; i < 3; i++) {
        if (i == 1) {
            continue;
        }
        accumulator.add(i);
    }

    int whileCount = 0;
    while (whileCount < 2) {
        whileCount++;
        shared++;
    }

    int doCount = 0;
    do {
        doCount++;
        shared++;
    } while (doCount < 1);

    switch (shared) {
        case 3:
            accumulator.add(10);
        case 4:
            accumulator.add(20);
            break;
        default:
            accumulator.add(100);
    }

    IScore@ score = accumulator;
    return score.score() + shared + 7;
}
