camview:
	mkdir -p output
	gcc --shared -fPIC -o output/camview -L/usr/lib/x86_64-linux-gnu -ldrm -ljson-c -I/usr/include/json-c -I/usr/include/libdrm -Isrc src/cec_controls.c src/control-file.c src/display.c src/jpeg_dec_main.c src/jpeg.c src/main.c src/memory.c src/ve.c

install:
	cp output/camview /bin	
	chmod 755 /bin/camview

clean:
	rm output/*
