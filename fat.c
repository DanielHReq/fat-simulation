#include "fat.h"
#include "ds.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define ERRO -1
#define DONE 0

#define SUPER 0
#define DIR 1
#define FAT 2
#define DATA 3

#define SIZE 1024


// Estrutura do Superbloco
#define MAGIC_N           0xAC0010DE
#define N_DATA_BLOCKS(x) (x.number_blocks - x.n_fat_blocks - 2)  // quantos blocos são usados para dados
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
	unsigned int first;			// primeiro bloco do arquivo como ÍNDICE DA FAT
} dir_item;

#define N_ITEMS (BLOCK_SIZE / sizeof(dir_item))	// quantos itens de diretório que cabem em um bloco
dir_item dir[N_ITEMS];			//variável para receber o diretório quando ele for para a RAM

// FAT
#define FREE 0
#define EOFF 1
#define BUSY 2
unsigned int *fat;				//Variável para trazer a FAT para RAM (tomar cuidado, alocação dinâmica)

int mountState = 0;				// 0 = n montado, 1 = montado

/**
 * Considerando que a FAT tem os números 0 e 1 reservados (livre e último bloco do arquivo, respectivamente)
 * não podemos indicar que o próximo bloco está nos índices 0 ou 1 da FAT
 * 
 * Para contornar isso, começaremos a contar o índice da FAT a partir de 2
 * 
 * Este novo valor, que será o preenchimento da FAT, será chamado de special
 */
#define FAT_INDEX_2_SPECIAL(x) x + 2
#define SPECIAL_2_FAT_INDEX(x) x - 2

/**
 * Similarmente ao anterior, também temos as conversões de índice para bloco
 */
#define BLOCK_2_FAT_INDEX(x) x - DATA
#define FAT_INDEX_2_BLOCK(x) x + DATA

#define SPECIAL_2_BLOCK(x) x + 1
#define BLOCK_2_SPECIAL(x) x - 1


// ====== FUNÇÕES AUXILIARES ======

/**
 * Recebe um buffer (char *) e a quantidade de caracteres e o preenche com zeros
 */
void zeros_buffer (char *buffer, unsigned int len) {

    FILE *file;
    if( !(file = fopen("/dev/zero","r")) ) {
        printf ("Não foi possível abrir '/dev/zero': %s\n", strerror(errno));
        return ERRO;
    }
    fread(buffer, len, 1, file);
    close(file);

}

/**
 * Retorna o índice no diretório se achar e -1 se não
 */
int search_item_by_name (char *name) {

    int i;
    for (i = 0; i < N_ITEMS; i++) {

        if (dir[i].used == OK) {
            if (strcmp(dir[i].name, name) == 0) return i;
        }
    }
    return ERRO;
}

// ======


/**
 * Cria novo sistema de arquivos no disco atual
 * 
 * 1. Criar superbloco
 * 2. Criar diretório
 * 3. Criar FAT
 * 
 * Não é possível formatar um sistema de arquivos já montado
 * 
 */
int fat_format(const char *filename){ 

    // se estiver montado, causa erro

    if (mountState) return ERRO;
    
    // preenche buffer com zeros

    char buffer[BLOCK_SIZE];
    zeros_buffer (buffer, BLOCK_SIZE);

    // escreve NULL em todos os blocos (exceto superbloco)

    for (int i = DIR; i < ds_size(); i++) {
        ds_write(SUPER + i, buffer);
    }

  	return 0;
}

/**
 * Depura o sistema de arquivos
 */
void fat_debug(){
	printf("depurando\n");

	super state_sb;						//Estado do Superbloco atual
	dir_item state_dir[N_ITEMS];		//Estado do diretório no momento
	unsigned int *state_FAT = NULL;	//Estado da FAT

	//tratamento do superbloco
	ds_read(SUPER, (char *)&state_sb);
	printf("---- | Superbloco | ---- \n");
	if(state_sb.magic == MAGIC_N){
		printf("Número Mágico está OK \n");
	}else{
		printf("Valor do Número mágico incorreto! (x%x)\n", state_sb.magic);
	}
	printf("Quantidade de blocos: %d\n", state_sb.number_blocks);
	printf("Quantidade de blocos da FAT: %d\n", state_sb.n_fat_blocks);
	
	//leitura da FAT no disco
	state_FAT = (unsigned int *)malloc(state_sb.number_blocks * BLOCK_SIZE);
	if(state_FAT == NULL){
		fprintf(stderr, "Erro: Falha na alocação de memória para a FAT durante depuração.\n");
        return;
	}

	//percorrer os blocos da FAT
	for(int i = 0; i < state_sb.n_fat_blocks; i++){
		ds_read(FAT + i, (char *) &state_FAT[i * (BLOCK_SIZE / sizeof(unsigned int))]);
	}
	
	//Ler diretório 
	ds_read(DIR, (char *)&state_dir);
	
	printf("---- | Diretório | ---- \n");
	for(int i = 0; i < N_ITEMS; i++){
		if(state_dir[i].used == OK){
            printf("Nome do Arquivo: %s\n", state_dir[i].name);
			printf("Tamanho: %d\n", state_dir[i].length);
			printf("Blocos: ");

			int fat_index = state_dir[i].first; // resgata o primeiro índice da FAT
            printf ("%d ", FAT_INDEX_2_BLOCK(fat_index)); // mostra o primeiro bloco (todo arquivo com estado 'usado' tem ao menos um bloco)

            int fat_special = state_FAT[fat_index]; // resgata o próximo dado
            
			while(fat_special != EOFF && fat_special != FREE && fat_special < state_sb.number_blocks){
				printf("%d ", SPECIAL_2_BLOCK(fat_special));
				fat_special = state_FAT[SPECIAL_2_FAT_INDEX(fat_special)];
			}
			printf("\n");
		}
	}
	printf("\n");
	free(state_FAT);
}

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
        //fat = (unsigned int *)malloc(sb.number_blocks * sizeof(unsigned int));
        fat = (unsigned int *)malloc(state_sb.number_blocks * BLOCK_SIZE);
        if (fat == NULL) {
			fprintf(stderr, "Falha na alocação de memória para a FAT\n");
			return ERRO;
		}
	}
	
	/* Trazer FAT para RAM */
	int entries_per_block = BLOCK_SIZE / sizeof(unsigned int);
	for(int i = 0; i < sb.n_fat_blocks; i++){
		// como lemos por blocos, e cada bloco possui 1024 entradas, inserimos 1024 entradas no nosso array da fat por vez
		// Vantagens da alocação dinâmica
		ds_read(FAT + i, (char *)&fat[i * entries_per_block]);
	}	

	/* Trazer diretório para a RAM */
	ds_read(DIR, (char*)&dir);

	mountState = 1;

	return DONE;


}

/**
 * Cria uma entrada de diretório descrevendo um arquivo vazio
 * 
 * name: string de até 6 bytes
 * 
 */
int fat_create(char *name){

    if (mountState == 0) return ERRO;

    if (strlen (name) > MAX_LETTERS) return ERRO;

    // se já houver item de mesmo nome, ERRO

    int i = search_item_by_name (name);
    if (i >= 0) return ERRO;

    // procura bloco vago

    for (i = 0; i < N_DATA_BLOCKS(sb); i++) {
        
        // se encontrar espaço vazio
        if (fat[i] == 0) break;
    }
    if (i == N_DATA_BLOCKS(sb)) return ERRO; // caso não encontre espaço
    
    int fat_index = i; // para atualizar FAT posteriormente

    // setup struct new_dir_item

    dir_item new_dir_item;
    new_dir_item.first = fat_index;
    strcpy (new_dir_item.name, name);
    new_dir_item.used = OK;
    new_dir_item.length = 0;

    for (i = 0; i < N_ITEMS; i++) {
        
        if (!dir[i].used) break; // se não usado
    }
    if (i == N_ITEMS) return ERRO;
    
    // atualizações
    
    dir[i] = new_dir_item;
    ds_write (DIR, (char *)&dir);
    fat[fat_index] = EOFF;
    ds_write (FAT, fat);
    
  	return 0;
}

/**
 * Remove o arquivo e libera todos os blocos associados à este
 * 
 * Atualiza a FAT no disco e RAM
 * 
 * Por último, libera a entrada no diretório
 * 
 * name: nome do arquivo
 * 
 */
int fat_delete(char *name){

    if (mountState == 0) return ERRO;

    if (strlen(name) > MAX_LETTERS) return ERRO;

    // procura o arquivo pelo nome

    int i = search_item_by_name (name);
    if (i == -1) return ERRO;
    
    // preenche buffer com zeros
    
    char buffer[BLOCK_SIZE];
    zeros_buffer (buffer, BLOCK_SIZE); // daqui em diante o buffer está preenchido com zeros
    
    unsigned int fat_index = dir[i].first;
    
    // remove o item do diretório
    
    dir[i].used = NON_OK; // marcado como não usado
    
    // remove os blocos associados

    unsigned int fat_special = FAT_INDEX_2_SPECIAL(fat_index);
    do {
        ds_write (SPECIAL_2_BLOCK(fat_special), buffer);
        fat_index = SPECIAL_2_FAT_INDEX(fat_special);
        fat_special = fat[fat_index];

        fat[fat_index] = 0; // liberar o endereço na FAT

    } while(fat_special != EOFF && fat_special != FREE);
    
    // atualiza a FAT no disco

    ds_write (FAT, fat);

    // atualiza o diretório

    ds_write (DIR, dir);

  	return 0;
}

/**
 * Devolve o tamanho em bytes do arquivo
 */
int fat_getsize(char *name){

    if (mountState == 0) return ERRO;

    if (strlen(name) > MAX_LETTERS) return ERRO;

    // procura o arquivo pelo nome

    int i = search_item_by_name (name);
    if (i == -1) return ERRO;

	return dir[i].length;
}

//Retorna a quantidade de caracteres lidos
int fat_read(char *name, char *buff, int length, int offset){
	return 0;
}

//Retorna a quantidade de caracteres escritos
int fat_write(char *name, const char *buff, int length, int offset){
	return 0;
}
