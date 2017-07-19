#include <stdio.h>
#include <stdlib.h>

#define err_exit(fmt, arg...)   \
	{	printf(fmt, ##arg);	\
		 exit(0);		\
	}

void useage(void)
{
	err_exit("./firmware2header <firmware file name>\n");
}

int main(int argc, char **argv)
{
	FILE *f_ip, *f_op;
	unsigned char byte;
	int ret, i = 1, count = 0;
	char op_filename[50];

	if (argc < 2)
		useage();

	f_ip = fopen(argv[1], "r");
	if (!f_ip)
		err_exit("failed to open %s\n", argv[1]);

	sprintf(op_filename, "%s.h", argv[1]);
	printf("%s\n", op_filename);

	f_op = fopen(op_filename, "w");
	if (!f_op)
		err_exit("failed to open output file\n");

	do {
		ret = fread(&byte, 1, 1, f_ip);
		if (ret < 0)
			exit(0);
		if (i%9 == 0)
			fprintf(f_op, "0x%02x,", byte);
		else
			fprintf(f_op, "0x%02x, ", byte);
		i++;
		/* New line once in every 10 elements */
		if (i%10 == 0) {
			i = 1;
			fprintf(f_op, "\n");
		}
		count++;
	} while (ret == 1);

	fclose(f_ip);
	fclose(f_op);

	printf("count = %d\n", count);
}
