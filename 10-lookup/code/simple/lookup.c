#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define MaxLine 100

struct TreeNode{
	int net_id;
	int prefix_len;
	int port;
	struct TreeNode *lchild;
	struct TreeNode *rchild;
	struct TreeNode *parent;
};

struct RouterEntry{
	int net_id;
	int prefix_len;
	int port;
};


struct TreeNode * init_tree(void);
struct TreeNode * insert_tree(struct RouterEntry * router, int prefix_len, struct TreeNode *parent);
struct RouterEntry * line_parser(char * line);
int char_to_int(char *c);
int add_node(struct RouterEntry * router, struct TreeNode * root);
int lookup(int net_id, struct TreeNode *root);
int get_mask(int prefix_len);
int look_back(struct TreeNode * node);

long mem_use=0;

int main() {
	FILE * fd = fopen("forwarding-table.txt","r");
	
	if (fd==NULL){
		printf("ERROR: File does not exist!");
		return 0;
	}
	
	char * line = (char *) malloc(MaxLine);
	
	struct TreeNode * root=NULL;
	root = init_tree();
	
	while(fgets(line, MaxLine, fd)!=NULL){
		struct RouterEntry * router=NULL;
		router = line_parser(line);
		add_node(router, root);
	}

	printf("INFORMATION: Memory used: %ld B =%ld KB =%ld MB\n",mem_use,mem_use/1024,mem_use/1024/1024);
	
	double time_use;
	double handle_time;
	double avg_time;
	struct timeval start, end;
	int input_num=0;
	
	//calculate time needed for processing input
	fseek(fd, 0, SEEK_SET);
	gettimeofday(&start, NULL);
	while(fgets(line,MaxLine,fd) != NULL){
		char * net_id=NULL;
		net_id = strtok(line, " ");
		char_to_int(net_id);
		input_num++;
	}
	gettimeofday(&end, NULL);
	time_use = (end.tv_sec-start.tv_sec)*1E9+(end.tv_usec-start.tv_usec)*1E3;
	handle_time = time_use / input_num;
	
	//calculate time needed in total
	input_num=0;
	fseek(fd, 0, SEEK_SET);
	gettimeofday(&start, NULL);
	while(fgets(line,MaxLine,fd) != NULL){
		char * net_id=NULL;
		net_id = strtok(line, " ");
		lookup(char_to_int(net_id), root);
		input_num++;
	}
	gettimeofday(&end, NULL);
	time_use = (end.tv_sec-start.tv_sec)*1E9+(end.tv_usec-start.tv_usec)*1E3;
	avg_time = time_use / input_num;
	
	printf("INFORMATION: Average time needed: %.9f ns\n", avg_time-handle_time);
	
	return 0;
	
}

struct TreeNode * init_tree(void){
	struct TreeNode * root = NULL;
	
	root = (struct TreeNode *)malloc(sizeof(struct TreeNode));
	root->net_id = 0;
	root->prefix_len = 0;
	root->port = 0;
	
	mem_use += sizeof(struct TreeNode);
	
	return root;
}

struct TreeNode * insert_tree(struct RouterEntry * router, int prefix_len, struct TreeNode *parent){
	struct TreeNode * node = (struct TreeNode *) malloc(sizeof(struct TreeNode));
	mem_use += sizeof(struct TreeNode);
	
	node->net_id = router->net_id & get_mask(prefix_len);
	node->prefix_len = prefix_len;
	if (router->prefix_len != prefix_len)
		node->port = -1;
	else 
		node ->port = router->port;
	
	node->parent = parent;
	return node;
}

struct RouterEntry * line_parser(char * line){
	char * net_id=NULL;
	char * prefix_len=NULL;
	char * port=NULL;
	struct RouterEntry * router = NULL;
	
	router = (struct RouterEntry *)malloc(sizeof(struct RouterEntry));
	net_id = strtok(line, " ");
	prefix_len = strtok(NULL, " ");
	port = strtok(NULL, " ");
	
	router->net_id = char_to_int(net_id);
	router->prefix_len = atoi(prefix_len);
	router->port = atoi(port);
	
	return router;
}

int char_to_int(char *c){
	unsigned int num=0;
	int i=0, j=0;
	
	for(j=0; j<3; j++,i++){
		num += atoi(&c[i]);
		num *= 256;
		
		while(i<strlen(c) && c[i]!='.')
			i++;
	}
	
	num += atoi(&c[i]);
	return num;
}

int add_node(struct RouterEntry * router, struct TreeNode * root){
	struct TreeNode * ptr = root;
	int mask = 0x80000000;
	unsigned int prefix_bit = 0x80000000;
	int insert_num = 0;
	int i=0;
	
	for(i=1; i<=router->prefix_len; i++){
		if ( (router->net_id & prefix_bit)==0 ){
			if (ptr->lchild == NULL){
				ptr->lchild = insert_tree(router, i, ptr);
				insert_num++;
			}
			ptr = ptr->lchild;
		}
		else{
			if (ptr->rchild == NULL){
				ptr->rchild = insert_tree(router, i, ptr);
				insert_num++;
			}
			ptr = ptr->rchild;
		}
		
		mask = mask>>1;
		prefix_bit = prefix_bit>>1;
	}
	
	if (insert_num==0)
		printf("net:%x update -> port: %d\n ", router->net_id&mask, ptr->port);
	
	if ((router->net_id&mask) != (ptr->net_id&mask))
		return -1;
	
	free(router);
	mem_use -= sizeof(struct RouterEntry);
	
	return ptr->port;
}

int lookup(int net_id, struct TreeNode *root){
	struct TreeNode * ptr=root;
	unsigned int prefix_bit = 0x80000000;
	int i;
	int port;
	
	for (i=1; i<32; i++){
		if ((net_id & prefix_bit) == 0){
			if (ptr->lchild == NULL){
				if ((net_id&get_mask(ptr->prefix_len)) != (ptr->net_id&get_mask(ptr->prefix_len))){
					port = -1;
					break;
				}
				port = ptr->port;
				break;
			}
			ptr = ptr->lchild;
		}
		else{
			if (ptr->rchild == NULL) {
				if ((net_id&get_mask(ptr->prefix_len)) != (ptr->net_id&get_mask(ptr->prefix_len))){
					port = -1;
					break;
				}
				port = ptr->port;
				break;
			} 
			ptr = ptr->rchild;
		}
		prefix_bit = prefix_bit >> 1;
	}
	
	if ((net_id&get_mask(i)) != (ptr->net_id&get_mask(i)))
		port = -1;
	
	if(port == -1)
		port = look_back(ptr);
	
	return port;
}

int get_mask(int prefix_len){
	int mask = 0x80000000;
	mask = mask >> (prefix_len-1);
	return mask;
}

int look_back(struct TreeNode * node){
	struct TreeNode * ptr = node;
	
	while (ptr->port == -1)
		ptr = ptr->parent;
	
	return ptr->port;
}