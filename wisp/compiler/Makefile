TARGET 	= main
FILES	= $(wildcard ./src/*.c)
OBJS    = $(subst /src/,/bin/, $(subst .c,.o, $(FILES)))
INCLUDE = -I./src/ -I../shared/

$(TARGET): $(OBJS) | ./bin
	gcc -o $(TARGET) $(OBJS)

./bin/%.o: ./src/%.c | ./bin
	gcc -o $@ $(INCLUDE) -c $^

./bin:
	mkdir -p ./bin

clean:
	rm -r ./bin
	rm $(TARGET)

run: $(TARGET)
	./$(TARGET) ./input.txt ../output.out