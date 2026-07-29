float calc(float a, float b)
{
    Print("Received: " + a + ", " + b + "\n");
    Print("System has been running for " + GetSystemTime() / 1000.0 + " seconds\n");
    return a * b;
}

