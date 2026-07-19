function benchmark(repetitions) {
    let checksum = 0;
    for (let r = 0; r < repetitions; r++) {
        let sum = 0;
        for (let i = 0; i < 1000000; i++) sum += i;
        checksum += sum;
    }
    return checksum;
}
