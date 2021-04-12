shell:
	mkdir -p build
	cd build && gcc -fsanitize=address -fsanitize=undefined -fno-sanitize-recover -fstack-protector -Wall -Wextra -Werror -Wno-missing-field-initializers ../source/mergesort.c -o mergesort.out -lrt

test: shell
	cd build && python3 ../checker/generator.py -f test1.txt -c 1000 -m 1000
	cd build && python3 ../checker/generator.py -f test2.txt -c 1000 -m 1000
	cd build && python3 ../checker/generator.py -f test3.txt -c 1000 -m 1000
	cd build && python3 ../checker/generator.py -f test4.txt -c 1000 -m 1000
	cd build && python3 ../checker/generator.py -f test5.txt -c 1000 -m 1000
	cd build && python3 ../checker/generator.py -f test6.txt -c 1000 -m 1000
	cd build && ./mergesort.out test1.txt test2.txt test3.txt test4.txt test5.txt test6.txt
	cd build && python3 ../checker/checker.py -f mergesorted.txt

clean:
	rm -rf build
