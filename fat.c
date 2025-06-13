#include "fat.h"
#include "ds.h"
#include <errno.h>
// #include <math.h> // Não é necessário
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include <strings.h> // Usar memset/memcpy do <string.h> para maior portabilidade
#include <unistd.h>

#define SUPER 0 // Bloco do Superbloco
#define DIR 1   // Bloco do Diretório
#define FAT 2   // Bloco inicial da FAT no disco
#define DATA 3  // Bloco inicial dos dados (primeiro bloco não-metadado)
// #define SIZE 1024 // Não parece ser usado, pode ser removido

// Estrutura do Superbloco
#define MAGIC_N 0xAC0010DE
// Macro para calcular o número de blocos de dados
// number_blocks - (SUPER + DIR + n_fat_blocks) = total - 2 (SUPER, DIR) - n_fat_blocks
// Ou, mais explicitamente: total_blocks - (1 superbloco + 1 diretório + n_fat_blocks)
#define N_DATA_BLOCKS(total_blocks, fat_blocks) (total_blocks - (FAT + fat_blocks)) 
typedef struct {
    int magic;
    int number_blocks;
    int n_fat_blocks; // quantos blocos a FAT ocupa
    char empty[BLOCK_SIZE - 3 * sizeof(int)];
} super;

super sb; // variável para receber o superbloco quando ele for para RAM

// item do diretório
#define MAX_LETTERS 6
#define OK 1
#define NON_OK 0
typedef struct {
    unsigned char used;          // usar OK ou NON_OK
    char name[MAX_LETTERS + 1];  // nome do arquivo, adicionado do terminador de string
    unsigned int length;         // tam do arq em bytes
    unsigned int first;          // primeiro bloco do arquivo na FAT (AGORA É O NÚMERO DO BLOCO FÍSICO)
} dir_item;

#define N_ITEMS (BLOCK_SIZE / sizeof(dir_item)) // quantos itens de diretório que cabem em um bloco
dir_item dir[N_ITEMS];           // variável para receber o diretório quando ele for para a RAM

// FAT
#define FREE 0 // Bloco livre
#define EOFF 1 // Fim de Arquivo
#define BUSY 2 // Bloco ocupado

#define ERRO -1
#define DONE 0

unsigned int * fat; // Variável para trazer a FAT para RAM (alocação dinâmica para TOTAL_BLOCKS)

int mountState = 0; // 0 = n montado, 1 = montado


// Recebe um buffer (char *) e a quantidade de caracteres e o preenche com zeros
//precirei mudar isso pq só funcionava no linux
void zeros_buffer(char *buffer, unsigned int len) {
    memset(buffer, 0, len);
}

// Retorna o índice no diretório se achar, e ERRO (-1), se não achar
int search_item_by_name(char *name) {
    int i;
    for (i = 0; i < N_ITEMS; i++) {
        if (dir[i].used == OK) {
            if (strcmp(dir[i].name, name) == 0) return i;
        }
    }
    return ERRO;
}

/* dado um arquivo em branco, deixar ele dentro do modelo */
int fat_format() {
    // se tentar formatar um arquivo montado, precisamos retornar erro
    if (mountState == 1) {
        printf("Erro: Tentativa de formatar um arquivo que está em memória (montado).\n");
        return ERRO;
    }

    int total_blocks = ds_size();
    printf("Formatando disco com %d blocos...\n", total_blocks);

    // quantos blocos a FAT vai ocupar?
    // cada entrada da FAT é um unsigned int (4 bytes)
    unsigned int fat_bytes_needed = total_blocks * sizeof(unsigned int);

    // o normal seria só dividir por BLOCK_SIZE, mas como um uso pequeno de um bloco "consome" um bloco inteiro, precisamos truncar pra cima por segurança
    unsigned int fat_blocks_needed = (fat_bytes_needed + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (fat_blocks_needed == 0) fat_blocks_needed = 1; // Garante que a FAT ocupe pelo menos 1 bloco

    // inicializar superbloco
    super init_sb;
    init_sb.magic = MAGIC_N;
    init_sb.number_blocks = total_blocks;
    init_sb.n_fat_blocks = fat_blocks_needed;

    // escrever superbloco no bloco 0 do arquivo
    ds_write(SUPER, (char *)&init_sb);
    printf("Superbloco inicializado no bloco %d.\n", SUPER);


    // inicializar o diretório
    dir_item init_dir_items[N_ITEMS]; // Usar um nome diferente para evitar conflito com a global 'dir'
    for (int i = 0; i < N_ITEMS; i++) {
        init_dir_items[i].used = NON_OK;    // espaço livre para uso
        init_dir_items[i].name[0] = '\0'; // sem nome
        init_dir_items[i].length = 0;     // sem ocupação de espaço
        init_dir_items[i].first = FREE;   // não há bloco ocupado (usando FREE=0)
    }
    // escrever diretório
    ds_write(DIR, (char *)&init_dir_items);
    printf("Diretório inicializado no bloco %d.\n", DIR);


    // inicializar a FAT completa (para o disco)
    // A FAT em disco e em RAM (nesta abordagem) tem o tamanho total_blocks
    unsigned int *init_fat_on_disk = (unsigned int *)malloc(total_blocks * sizeof(unsigned int));
    if (init_fat_on_disk == NULL) {
        fprintf(stderr, "Erro ao alocar memória para a FAT inicial no disco.\n");
        return ERRO;
    }

    // preencher a fat com os estados iniciais
    for (int i = 0; i < total_blocks; i++) {
        // Blocos de metadados: SUPER, DIR, e os blocos que a FAT ocupa
        if (i == SUPER || i == DIR || (i >= FAT && i < (FAT + fat_blocks_needed))) {
            init_fat_on_disk[i] = BUSY; // Ocupados por metadados do sistema
        } else {
            init_fat_on_disk[i] = FREE; // Livres para dados
        }
    }

    // escrever FAT, aqui estamos escrevendo por blocos de uma vez
    int entries_per_fat_block = BLOCK_SIZE / sizeof(unsigned int);
    for (int i = 0; i < fat_blocks_needed; i++) {
        // FAT + i é o número do bloco físico no disco
        // &init_fat_on_disk[i * entries_per_fat_block] é o ponteiro para o início dos dados do bloco da FAT
        ds_write(FAT + i, (char *)&init_fat_on_disk[i * entries_per_fat_block]);
    }
    printf("FAT inicializada nos blocos %d a %d.\n", FAT, FAT + fat_blocks_needed - 1);

    free(init_fat_on_disk); // Liberar a memória temporária

    printf("Formatação concluída com sucesso.\n");
    return DONE;
}


void fat_debug() {
    printf("DEPURANDO...\n");

    super state_sb;                 // Estado do Superbloco atual
    dir_item state_dir[N_ITEMS];    // Estado do diretório no momento
    unsigned int *state_FAT = NULL; // Estado da FAT (lida diretamente do disco para debug)

    // tratamento do superbloco
    ds_read(SUPER, (char *)&state_sb);
    printf("---- | Superbloco | ---- \n");
    if (state_sb.magic == MAGIC_N) {
        printf("Número Mágico está OK \n");
    } else {
        printf("Valor do Número mágico incorreto! (x%x)\n", state_sb.magic);
    }
    printf("Quantidade de blocos: %d\n", state_sb.number_blocks);
    printf("FAT está usando %d blocos\n", state_sb.n_fat_blocks);
    printf("Blocos de dados disponíveis: %d\n", N_DATA_BLOCKS(state_sb.number_blocks, state_sb.n_fat_blocks));

    
    // leitura da FAT do disco (completa)
    state_FAT = (unsigned int *)malloc(state_sb.number_blocks * sizeof(unsigned int));
    if (state_FAT == NULL) {
        fprintf(stderr, "Erro: Falha na alocação de memória para a FAT durante depuração.\n");
        return;
    }

    // percorrer os blocos da FAT e ler
    int entries_per_block = BLOCK_SIZE / sizeof(unsigned int);
    for (int i = 0; i < state_sb.n_fat_blocks; i++) {
        ds_read(FAT + i, (char *)&state_FAT[i * entries_per_block]);
    }

    // Ler diretório
    ds_read(DIR, (char *)&state_dir);

    printf("---- | Diretório | ---- \n");
    for (int i = 0; i < N_ITEMS; i++) {
        if (state_dir[i].used == OK) {
            printf("Nome do Arquivo: %s\n", state_dir[i].name);
            printf("Tamanho: %d bytes\n", state_dir[i].length); // Adicionado 'bytes' para clareza
            printf("Blocos: ");

            // `first` é o número do bloco físico
            unsigned int current_block = state_dir[i].first;

            // Loop enquanto o bloco atual não for EOFF e for um bloco válido (não FREE ou BUSY ou fora dos limites)
            // Lembre-se que FREE, EOFF, BUSY são 0, 1, 2. Se um bloco aponta para 0, 1 ou 2, é um terminador ou metadado.
            while (current_block != EOFF && current_block != FREE && current_block >= DATA && current_block < state_sb.number_blocks) {
                printf("%d ", current_block);
                
                // O valor em state_FAT[current_block] pode ser o próximo bloco, EOFF, ou FREE
                if (state_FAT[current_block] == EOFF) {
                    break; // Fim da cadeia
                }
                
                // Se não é EOFF, significa que aponta para o próximo bloco.
                // Mas, precisamos garantir que o valor apontado não é FREE ou BUSY por engano.
                // Um link válido deve ser um número de bloco físico >= DATA.
                if (state_FAT[current_block] == FREE || state_FAT[current_block] == BUSY || state_FAT[current_block] < DATA) {
                     // Isso indica um erro na FAT, um bloco de dados apontando para algo inválido
                     printf(" (FAT error: links to %u) ", state_FAT[current_block]);
                     break; 
                }
                
                current_block = state_FAT[current_block]; // Move para o próximo bloco na cadeia
            }
            printf("\n");
        }
    }
    printf("\n");
    free(state_FAT); // Liberar a memória da FAT lida para depuração
}

int fat_mount() {
    printf("---- Iniciando Montagem... ----\n");
    
    if(mountState == 1){
        printf("Erro: Arquivo já está montado! \n");
        return ERRO;
    }
    super state_sb;
    ds_read(SUPER, (char *)&state_sb);

    // 1. Verificar número mágico do Superbloco
    if (state_sb.magic != MAGIC_N) {
        printf("Número mágico inválido. FAT inválida.\n");
        return ERRO;
    }

    // 2. Trazer Superbloco para RAM (variável global `sb`)
    memcpy(&sb, &state_sb, sizeof(super));
    printf("Superbloco montado. Total de blocos: %d, Blocos FAT: %d, Blocos de dados: %d.\n",
           sb.number_blocks, sb.n_fat_blocks, N_DATA_BLOCKS(sb.number_blocks, sb.n_fat_blocks));

    fat = (unsigned int *)malloc(sb.number_blocks * sizeof(unsigned int));
    if (fat == NULL) {
        fprintf(stderr, "Erro: Falha na alocação de memória para a FAT em RAM.\n");
        return ERRO;
    }

    // 4. Trazer a FAT do disco para a RAM (variável global `fat`)
    //int entries_per_block = BLOCK_SIZE / sizeof(unsigned int);
    for (int i = 0; i < sb.n_fat_blocks; i++) {
        ds_read(FAT + i, (char *)fat + (i * BLOCK_SIZE));
    }

    // 5. Trazer Diretório para RAM (variável global `dir`)
    ds_read(DIR, (char *)&dir);
    printf("Diretório montado.\n");

    mountState = 1;
    printf("Sistema de arquivos montado com sucesso!\n");
    return DONE;
}


int fat_create(char *name) {
    if (mountState == 0) {
        printf("Erro: Sistema de arquivos não montado.\n");
        return ERRO;
    }
    if (strlen(name) > MAX_LETTERS) {
        printf("Erro: Nome do arquivo '%s' excede o limite de %d caracteres.\n", name, MAX_LETTERS);
        return ERRO;
    }

    // 1. Procurar se o arquivo já existe
    if (search_item_by_name(name) != ERRO) {
        printf("Erro: Arquivo com nome '%s' já existe.\n", name);
        return ERRO;
    }

    // 2. Encontrar uma entrada livre no diretório
    int dir_idx = ERRO;
    for (int i = 0; i < N_ITEMS; i++) {
        if (dir[i].used == NON_OK) {
            dir_idx = i;
            break;
        }
    }
    if (dir_idx == ERRO) {
        printf("Erro: Diretório cheio, não pode criar mais arquivos.\n");
        return ERRO;
    }

    // 3. Encontrar o primeiro bloco de dados livre na FAT
    int free_block = ERRO;
    // O bloco 0 é SUPER, 1 é DIR, 2 é FAT. Dados começam em DATA (3).
    // Usar sb.n_fat_blocks que já está carregado na RAM.
    int first_data_block_idx = FAT + sb.n_fat_blocks;
    for (int i = first_data_block_idx; i < sb.number_blocks; i++) {
        if (fat[i] == FREE) { // CORRIGIDO: de = para ==
            free_block = i;
            break;
        }
    }
    if (free_block == ERRO) {
        printf("Erro: Não há blocos livres para criar o arquivo.\n");
        return ERRO;
    }

    // 4. Setup da nova entrada de diretório
    dir[dir_idx].first = free_block; // Armazena o número do bloco físico
    strcpy(dir[dir_idx].name, name);
    dir[dir_idx].used = OK;
    dir[dir_idx].length = 0; // Arquivo vazio inicialmente

    // 5. Atualizar o diretório em RAM e no disco
    ds_write(DIR, (char *)&dir);

    // 6. Atualizar a FAT em RAM: marcar o primeiro bloco como EOFF (fim do arquivo)
    fat[free_block] = EOFF;

    // 7. Escrever a FAT atualizada de volta ao disco
    int entries_per_block = BLOCK_SIZE / sizeof(unsigned int);
    for (int i = 0; i < sb.n_fat_blocks; i++) {
        ds_write(FAT + i, (char *)&fat[i * entries_per_block]);
    }

    printf("Arquivo '%s' criado com sucesso no bloco físico %d.\n", name, free_block);
    return DONE;
}


int fat_delete(char *name) {
    if (mountState == 0) {
        printf("Erro: Sistema de arquivos não montado.\n");
        return ERRO;
    }
    if (strlen(name) > MAX_LETTERS) { // Checagem de tamanho de nome já deve ser feita na criação
        printf("Erro: Nome do arquivo '%s' excede o limite de %d caracteres.\n", name, MAX_LETTERS);
        return ERRO;
    }

    // 1. Procurar o arquivo pelo nome
    int index_dir = search_item_by_name(name);
    if (index_dir == ERRO) {
        printf("Erro: Arquivo '%s' não encontrado para deletar.\n", name);
        return ERRO;
    }

    // 2. Buffer para zerar os blocos de dados
    char zero_buffer[BLOCK_SIZE];
    zeros_buffer(zero_buffer, BLOCK_SIZE);

    // 3. Percorrer a cadeia de blocos do arquivo, zerar o bloco de dados e liberar na FAT
    unsigned int current_block = dir[index_dir].first; // Primeiro bloco físico do arquivo
    unsigned int next_block_in_chain;

    printf("Deletando arquivo '%s', liberando blocos: ", name);

    // Loop enquanto o bloco atual for um bloco de dados válido (não EOFF, FREE, BUSY, nem metadado)
    // EOFF e FREE são terminadores válidos. BUSY e números < DATA são inválidos para dados.
    while (current_block != EOFF && current_block != FREE && current_block >= DATA && current_block < sb.number_blocks) {
        // Zera o conteúdo do bloco de dados físico no disco
        ds_write(current_block, zero_buffer);
        printf("%d ", current_block);

        // Guarda o próximo bloco na cadeia ANTES de liberar o bloco atual
        next_block_in_chain = fat[current_block];

        // Libera o bloco atual na FAT em RAM
        fat[current_block] = FREE;

        // Move para o próximo bloco na cadeia
        current_block = next_block_in_chain;
    }
    printf("\n");

    // 4. Escrever a FAT atualizada de volta ao disco
    int entries_per_block = BLOCK_SIZE / sizeof(unsigned int);
    for (int i = 0; i < sb.n_fat_blocks; i++) {
        ds_write(FAT + i, (char *)&fat[i * entries_per_block]);
    }

    // 5. Liberar a entrada no diretório em RAM e no disco
    dir[index_dir].used = NON_OK;       // Marcado como não usado
    dir[index_dir].name[0] = '\0';      // Limpar nome
    dir[index_dir].length = 0;          // Resetar tamanho
    dir[index_dir].first = FREE;        // Limpar ponteiro
    ds_write(DIR, (char *)dir);

    printf("Arquivo '%s' deletado com sucesso.\n", name);
    return DONE;
}


int fat_getsize(char *name) {
    if (mountState == 0) {
        printf("Erro: Sistema de arquivos não montado.\n");
        return ERRO;
    }

    int index_dir = search_item_by_name(name);
    if (index_dir == ERRO) {
        printf("Erro: Arquivo '%s' não encontrado.\n", name);
        return ERRO;
    }

    return dir[index_dir].length;
}

//Retorna a quantidade de caracteres lidos
int fat_read(char *name, char *buff, int length, int offset) {
    if (mountState == 0) {
        printf("Erro: Sistema de arquivos não montado.\n");
        return ERRO;
    }

    int index_dir = search_item_by_name(name);
    if (index_dir == ERRO) {
        printf("Erro: Arquivo '%s' não encontrado para leitura.\n", name);
        return ERRO;
    }

    // Não tentar ler além do tamanho do arquivo
    if (offset < 0 || offset >= dir[index_dir].length) {
        printf("Offset inválido: %d \n", offset); // Pode ser útil para debug, mas não para o usuário final
        return 0; // Nada para ler, 0 bytes lidos
    }

    if (length <= 0) {
        // printf("Valor de 'length' inválido: %d \n", length); // Pode ser útil para debug
        return 0; // Nada para ler, 0 bytes lidos
    }

    if (offset + length > dir[index_dir].length) {
        length = dir[index_dir].length - offset; // Ajusta o comprimento para não ler além do arquivo
    }

    unsigned int current_block = dir[index_dir].first;
    int bytes_read = 0;
    int current_offset_in_file = 0;

    // Buscar bloco inicial da leitura
    // Primeiro, avançar current_offset_in_file e current_block até o ponto inicial do offset
    while (current_offset_in_file + BLOCK_SIZE <= offset &&
           current_block != EOFF && current_block != FREE &&
           current_block >= DATA && current_block < sb.number_blocks) {
        current_offset_in_file += BLOCK_SIZE;
        current_block = fat[current_block];
    }

    // Agora que estamos no bloco correto, comece a ler
    while (bytes_read < length &&
           current_block != EOFF && current_block != FREE &&
           current_block >= DATA && current_block < sb.number_blocks) {

        int block_offset = offset - current_offset_in_file; // Offset dentro do bloco atual
        int bytes_to_read_in_block = BLOCK_SIZE - block_offset;
        if (bytes_to_read_in_block > (length - bytes_read)) {
            bytes_to_read_in_block = (length - bytes_read);
        }

        char temp_block_buffer[BLOCK_SIZE];

        // Ler bloco em disco
        ds_read(current_block, temp_block_buffer);

        memcpy(buff + bytes_read, temp_block_buffer + block_offset, bytes_to_read_in_block);
        bytes_read += bytes_to_read_in_block;
        offset += bytes_to_read_in_block; // Atualiza o offset geral para o próximo chunk

        current_offset_in_file += BLOCK_SIZE; // Atualiza o offset lógico do arquivo

        // Move para o próximo bloco na cadeia, se necessário
        if (bytes_read < length) { // Se ainda há mais para ler
            if (fat[current_block] == EOFF) { // Fim do arquivo inesperado
                break; 
            }
            if (fat[current_block] == FREE || fat[current_block] == BUSY || fat[current_block] < DATA) {
                fprintf(stderr, "Erro na FAT: Bloco %d aponta para um valor inválido (%u) durante leitura.\n", current_block, fat[current_block]);
                break;
            }
            current_block = fat[current_block];
        }
    }
    return bytes_read;
}

// Retorna a quantidade de caracteres escritos
int fat_write(char *name, const char *buff, int length, int offset) {
    if (mountState == 0) {
        printf("Erro: Sistema de arquivos não montado.\n");
        return ERRO;
    }

    int index_dir = search_item_by_name(name);
    // se não existir arquivo, criar um novo
    if (index_dir == ERRO) {
        if (fat_create(name) == ERRO) {
            printf("Erro: Não foi possível criar o arquivo '%s' para escrita.\n", name);
            return ERRO;
        }
        // dps de criar o novo arquivo, o index_dir será o do novo arquivo
        index_dir = search_item_by_name(name);
        if (index_dir == ERRO) { // Isso não deveria acontecer se fat_create for bem-sucedido
            printf("Erro: Arquivo recém-criado '%s' não encontrado no diretório.\n", name);
            return ERRO;
        }
    }

    unsigned int current_block = dir[index_dir].first;
    int bytes_escritos = 0;
    int current_file_length = dir[index_dir].length;
    int current_offset_in_file = 0; // offset lógico acumulado desde o início do arquivo

    // Se offset é maior que o tamanho atual do arquivo, preenche com zeros (extensão)
    if (offset > current_file_length) {
        int fill_length = offset - current_file_length;

        // Navegar até o ponto de início do preenchimento
        // current_block já é o primeiro bloco do arquivo quando a função inicia
        // current_offset_in_file começa em 0.
        // Precisamos avançar current_block e current_offset_in_file até o bloco
        // onde o preenchimento deve começar (o primeiro bloco após current_file_length).
        while (current_offset_in_file + BLOCK_SIZE <= current_file_length &&
               current_block != EOFF && current_block != FREE &&
               current_block >= DATA && current_block < sb.number_blocks) {
            current_offset_in_file += BLOCK_SIZE;
            current_block = fat[current_block];
        }

        // Loop para preencher os "buracos" com 0
        while (fill_length > 0) {
            unsigned int block_to_fill = current_block;

            // Se current_block for EOFF ou FREE, precisamos alocar um novo bloco.
            if (block_to_fill == EOFF || block_to_fill == FREE) {
                int new_block_num = ERRO;
                for (int i = DATA; i < sb.number_blocks; i++) {
                    if (fat[i] == FREE) {
                        new_block_num = i;
                        break;
                    }
                }
                if (new_block_num == ERRO) {
                    printf("Erro: Não há blocos disponíveis para estender o arquivo com zeros.\n");
                    // Escreve as mudanças na FAT antes de retornar, caso algum bloco já tenha sido alocado.
                    int entries_per_block_fat = BLOCK_SIZE / sizeof(unsigned int);
                    for (int i = 0; i < sb.n_fat_blocks; i++) {
                        ds_write(FAT + i, (char *)&fat[i * entries_per_block_fat]);
                    }
                    return bytes_escritos; // Retorna o que já foi escrito
                }

                fat[new_block_num] = EOFF; // Novo bloco é o fim da cadeia por enquanto

                // Se o arquivo estava vazio (dir[index_dir].first era EOFF ou FREE)
                if (dir[index_dir].first == EOFF || dir[index_dir].first == FREE) {
                    dir[index_dir].first = new_block_num;
                } else {
                    // Ligar o último bloco da cadeia existente ao novo bloco
                    unsigned int temp_block = dir[index_dir].first;
                    while (fat[temp_block] != EOFF) {
                        temp_block = fat[temp_block];
                    }
                    fat[temp_block] = new_block_num;
                }
                block_to_fill = new_block_num; // O bloco a ser preenchido agora é o novo
                current_block = new_block_num; // Atualiza current_block para o novo bloco para a próxima iteração
            }

            char temp_block_buffer[BLOCK_SIZE];
            // Para preenchimento, precisamos garantir que o bloco esteja zerado.
            // Se for um bloco existente, lemos, zeramos a parte relevante e escrevemos.
            // Se for um bloco novo, zeramos todo ele.
            ds_read(block_to_fill, temp_block_buffer); // Ler o bloco existente para não apagar dados que já estão lá.

            int block_fill_start_offset = current_file_length % BLOCK_SIZE; 
            // Se current_file_length é um múltiplo de BLOCK_SIZE, e não é o primeiro bloco do arquivo, 
            // significa que estamos começando a preencher um novo bloco do início.
            if (current_file_length % BLOCK_SIZE == 0 && current_file_length > 0) {
                 block_fill_start_offset = 0;
            }

            int bytes_to_fill_in_block = BLOCK_SIZE - block_fill_start_offset;
            if (bytes_to_fill_in_block > fill_length) {
                bytes_to_fill_in_block = fill_length;
            }
            
            memset(temp_block_buffer + block_fill_start_offset, 0, bytes_to_fill_in_block);
            ds_write(block_to_fill, temp_block_buffer);

            fill_length -= bytes_to_fill_in_block;
            current_file_length += bytes_to_fill_in_block; // Atualiza o comprimento do arquivo

            // Avança o offset lógico para o próximo bloco.
            // O current_block já foi atualizado para o bloco que acabou de ser preenchido,
            // então a próxima iteração do while(fill_length > 0) o usará ou alocará um novo.
            current_offset_in_file += bytes_to_fill_in_block; // Avança o offset lógico por bytes_to_fill_in_block, não BLOCK_SIZE
        }
    }

    // Agora, escreve os dados reais
    // Reajustar current_block e current_offset_in_file para o ponto de início da escrita `offset`
    current_block = dir[index_dir].first;
    current_offset_in_file = 0;

    // Navega até o bloco correto para o offset de escrita real (pós-preenchimento)
    while (current_offset_in_file + BLOCK_SIZE <= offset &&
           current_block != EOFF && current_block != FREE &&
           current_block >= DATA && current_block < sb.number_blocks) {
        current_offset_in_file += BLOCK_SIZE;
        current_block = fat[current_block];
    }
    
    // Loop para a escrita dos dados
    while (bytes_escritos < length) {
        // Se o arquivo terminou ou a cadeia FAT está inválida (EOFF, FREE, < DATA ou > sb.number_blocks)
        if (current_block == EOFF || current_block == FREE || current_block < DATA || current_block >= sb.number_blocks) {
            int new_block_num = ERRO;
            for (int i = DATA; i < sb.number_blocks; i++) {
                if (fat[i] == FREE) {
                    new_block_num = i;
                    break;
                }
            }
            if (new_block_num == ERRO) {
                printf("Erro: Sem blocos livres para escrita de dados.\n");
                break; // Não pode alocar mais blocos, termina a escrita
            }

            fat[new_block_num] = EOFF; // Novo bloco é o fim da cadeia por enquanto

            if (dir[index_dir].first == EOFF || dir[index_dir].first == FREE) { // Arquivo estava vazio ou esvaziado
                dir[index_dir].first = new_block_num;
                current_block = new_block_num; // Define o bloco atual como o novo primeiro bloco
            } else { // Linka o último bloco da cadeia existente ao novo bloco
                unsigned int temp_block = dir[index_dir].first;
                while (fat[temp_block] != EOFF) {
                    temp_block = fat[temp_block];
                }
                fat[temp_block] = new_block_num;
                current_block = new_block_num; // O bloco atual para escrita é o recém-alocado
            }
        }

        // Escreve no bloco atual
        char temp_block_buffer[BLOCK_SIZE];
        ds_read(current_block, temp_block_buffer); // Lê o conteúdo existente do bloco

        int block_offset = offset - current_offset_in_file;
        int bytes_to_write_in_block = BLOCK_SIZE - block_offset;
        if (bytes_to_write_in_block > (length - bytes_escritos)) {
            bytes_to_write_in_block = (length - bytes_escritos);
        }

        memcpy(temp_block_buffer + block_offset, buff + bytes_escritos, bytes_to_write_in_block);
        ds_write(current_block, temp_block_buffer);

        bytes_escritos += bytes_to_write_in_block;
        offset += bytes_to_write_in_block;

        // Atualiza o tamanho do arquivo se a escrita o estendeu
        if (offset > current_file_length) {
            current_file_length = offset;
        }
        
        current_offset_in_file += BLOCK_SIZE; // Avança o offset lógico para o próximo bloco

        // Move para o próximo bloco na cadeia para a próxima iteração de escrita
        if (fat[current_block] == EOFF) { // Se o bloco atual é o fim da cadeia, e ainda há dados para escrever,
                                          // a próxima iteração tentará alocar um novo bloco.
            current_block = EOFF; // Define para EOFF para que o próximo loop aloque
        } else {
            current_block = fat[current_block];
        }
    }

    dir[index_dir].length = current_file_length; // Atualiza o comprimento final do arquivo

    // Escrever o diretório e a FAT de volta ao disco após as modificações
    ds_write(DIR, (char *)&dir);
    int entries_per_block_fat = BLOCK_SIZE / sizeof(unsigned int);
    for (int i = 0; i < sb.n_fat_blocks; i++) {
        ds_write(FAT + i, (char *)&fat[i * entries_per_block_fat]);
    }

    return bytes_escritos;
}