#include "fat.h"
#include "ds.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define SUPER 0
#define DIR 1
#define FAT 2

#define SIZE 1024


// Estrutura do Superbloco
#define MAGIC_N           0xAC0010DE
typedef struct{
	int magic;
	int number_blocks;
	int n_fat_blocks;			//quantos blocos a FAT ocupa
	char empty[BLOCK_SIZE-3*sizeof(int)];
} super;

super sb;						//variável para receber o superbloco quando ele for para RAM

// item do diretório
#define MAX_LETTERS 6
#define OK 1
#define NON_OK 0
typedef struct{
	unsigned char used;			// usar OK ou NON_OK
	char name[MAX_LETTERS+1];	// nome do arquivo, adionado do terminador de string
	unsigned int length;		// tam do arq em bytes
	unsigned int first;			// primeiro bloco do arquivo na FAT
} dir_item;

#define N_ITEMS (BLOCK_SIZE / sizeof(dir_item))	// quantos itens de diretório que cabem em um bloco
dir_item dir[N_ITEMS];			//variável para receber o diretório quando ele for para a RAM

// FAT
#define FREE 0
#define EOFF 1
#define BUSY 2
unsigned int *fat;				//Variável para trazer a FAT para RAM (tomar cuidado, alocação dinâmica)

int mountState = 0;				// 0 = n montado, 1 = montado

int fat_format(){ 
  	return 0;
}

void fat_debug(){
	printf("depurando\n");

	super state_sb;						//Estado do Superbloco atual
	dir_item state_dir[N_ITEMS];		//Estado do diretório no momento
	unsigned int * state_FAT = NULL;	//Estado da FAT

	//tratamento do superbloco
	ds_read(SUPER, (char *)&state_sb);
	printf("---- | Superbloco | ---- \n");
	if(state_sb.magic == MAGIC_N){
		printf("Número Mágico está OK \n");
	}else{
		printf("Valor do Número mágico incorreto! (x%x)\n", state_sb.magic);
	}
	printf("Quantidade de blocos: %d\n", state_sb.number_blocks);
	printf("FAT está usando %d blocos\n", state_sb.n_fat_blocks);
	
	//leitura da FAT no disco
	state_FAT = (unsigned int *)malloc(state_sb.number_blocks * sizeof(unsigned int));
	if(state_FAT == NULL){
		fprintf(stderr, "Erro: Falha na alocação de memória para a FAT durante depuração.\n");
        return;
	}

	//percorrer os blocos da FAT
	for(int i = 0; i < state_sb.n_fat_blocks; i++){
		ds_read(FAT + i, (char *)&state_FAT[i * (BLOCK_SIZE/ sizeof(unsigned int))]);
	}
	
	//Ler diretório 
	ds_read(DIR, (char *)&state_dir);
	
	printf("---- | Diretório | ---- \n");
	for(int i = 0; i < N_ITEMS; i++){
		if(state_dir[i].used == OK){
			printf("Nome do Arquivo: %s\n", state_dir[i].name);
			printf("Tamanho: %d\n", state_dir[i].length);
			printf("Blocos: ");

			int blocos = state_dir[i].first;
			while(blocos != EOFF && blocos != FREE && blocos < state_sb.number_blocks){
				printf("%d ", blocos);
				if(state_FAT[blocos] == EOFF) break;
				blocos = state_FAT[blocos];
			}
			printf("\n");
		}
	}
	printf("\n");
	free(state_FAT);
}

#define ERRO -1
#define DONE 0
int fat_mount(){
	printf("---- Iniciando Montagem... ----\n");
  	super state_sb;
	ds_read(SUPER, (char*)&state_sb);
	if(state_sb.magic != MAGIC_N){
		printf("Número mágico inválido. FAT inválida \n");
		return ERRO;
	}

	//trazer Superbloco para memória (sb é a variável global que o recebe, declarada no começo do código)
	memcpy(&sb, &state_sb, sizeof(super));

	
	//alocar memória para a FAT
	if(fat == NULL){
		fat = (unsigned int *)malloc(sb.number_blocks * sizeof(unsigned int));
		if (fat == NULL) {
			fprintf(stderr, "Falha na alocação de memória para a FAT\n");
			return ERRO;
		}
	}
	
	/* Trazer FAT para RAM */
	int entries_per_block = BLOCK_SIZE / sizeof(unsigned int);
	for(int i = 0; i < sb.n_fat_blocks; i++){
		// como lemos por blocos, e cada bloco possue 1024 entradas, inserimos 1024 entradas no nosso array da fat por vez
		// Vantagens da alocação dinâmica
		ds_read(FAT + i, (char *)&fat[i * entries_per_block]);
	}	

	/* Trazer diretório para a RAM */
	ds_read(DIR, (char*)&dir);

	mountState = 1;

	return DONE;


}

int fat_create(char *name){
  	return 0;
}

int fat_delete( char *name){
  	return 0;
}

int fat_getsize( char *name){ 
	return 0;
}

//Retorna a quantidade de caracteres lidos
int fat_read( char *name, char *buff, int length, int offset){
	return 0;
}

//Retorna a quantidade de caracteres escritos
int fat_write( char *name, const char *buff, int length, int offset){
	return 0;
}
