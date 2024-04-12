PORT=57521
CFLAGS= -DPORT=\$(PORT) -g -Wall

battle: battle.c
	gcc $(CFLAGS) -o battle battle.c