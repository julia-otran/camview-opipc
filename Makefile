camview:
	mkdir -p output
	gcc -fPIC -I/usr/include/json-c -I/usr/include/libdrm -Isrc src/cec_controls.c src/control-file.c src/display.c src/jpeg_dec_main.c src/jpeg.c src/main.c src/memory.c src/ve.c -L/usr/lib/arm-linux-gnueabihf -lm -ldrm -ljson-c -o output/camview

install:
	cp output/camview /bin	
	chmod 755 /bin/camview

clean:
	rm output/*
