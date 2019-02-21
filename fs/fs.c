#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>

#include "fs.h"
#include "../tools/rbtree.h"

struct fs_super *fs_sb = NULL;

uint32_t generate_unique_id()
{
	fs_sb->curr_dir_id++;
	return fs_sb->curr_dir_id;
}

void set_dentry_flag(struct dentry *dentry, int flag_type, int val)
{
	if (val == 0)
	{
		// set 0
		dentry->flags &= ~(1UL << flag_type);
	} else {
		// set 1
		dentry->flags |= 1UL << flag_type;
	}
}

int get_dentry_flag(struct dentry *dentry, int flag_type)
{
	if (((dentry->flags >> flag_type) & 1) == 1 )
	{
		return 1;
	} else {
		return 0;
	}
}

int add_dentry_to_dirty_list(struct dentry *dentry)
{
	set_dentry_flag(dentry, D_dirty, 1);
	struct dirty_dentry *dirty_dentry = (struct dirty_dentry *) calloc(1, sizeof(struct dirty_dentry));
	dirty_dentry->dentry = dentry;
	dirty_dentry->prev = NULL;
	dirty_dentry->next = NULL;

	if (fs_sb->dirty_dentry_head == NULL) {
		fs_sb->dirty_dentry_head = dirty_dentry;
		fs_sb->dirty_dentry_tail = dirty_dentry;
		return 0;
	}

	dirty_dentry->next = fs_sb->dirty_dentry_head;
	fs_sb->dirty_dentry_head->prev = dirty_dentry;
	fs_sb->dirty_dentry_head = dirty_dentry;
	return 0;
}

int remove_dentry_from_dirty_list(struct dentry *dentry)
{
	struct dirty_dentry * dirty_dentry = fs_sb->dirty_dentry_tail;
	if (get_dentry_flag(dentry, D_type) == DIR_DENTRY) {
		while (dirty_dentry->dentry->inode != dentry->inode && dirty_dentry != NULL)
			dirty_dentry = dirty_dentry->prev;
	} else {
		while (dirty_dentry->dentry->fid != dentry->fid && dirty_dentry != NULL)
			dirty_dentry = dirty_dentry->prev;
	}

	if (dirty_dentry == NULL) {
		printf("this dentry not in dirty list\n");
		return 0;
	}
	if (dirty_dentry == fs_sb->dirty_dentry_tail) {
		if (dirty_dentry->prev == NULL) {
			fs_sb->dirty_dentry_head = NULL;
			fs_sb->dirty_dentry_tail = NULL;
			goto out;
		}
		fs_sb->dirty_dentry_tail = dirty_dentry->prev;
		dirty_dentry->prev->next = NULL;
	} else {
		dirty_dentry->next->prev = dirty_dentry->prev;
		if (dirty_dentry->prev != NULL) {
			dirty_dentry->prev->next = dirty_dentry->next;
		} else {
			fs_sb->dirty_dentry_head = dirty_dentry->next;
		}
	}
out:
	set_dentry_flag(dentry, D_dirty, 0);
	
	free(dirty_dentry);
	dirty_dentry = NULL;
	return 0;	
}

// front insert
int add_dentry_to_unused_list(struct dentry *dentry)
{
	set_dentry_flag(dentry, D_dirty, 0);
	struct unused_dentry *unused_dentry = (struct unused_dentry *) calloc(1, sizeof(struct unused_dentry));
	unused_dentry->dentry = dentry;
	unused_dentry->prev = NULL;
	unused_dentry->next = NULL;

	if (fs_sb->unused_dentry_head == NULL) {
		fs_sb->unused_dentry_head = unused_dentry;
		fs_sb->unused_dentry_tail = unused_dentry;
		return 0;
	}

	unused_dentry->next = fs_sb->unused_dentry_head;
	fs_sb->unused_dentry_head->prev = unused_dentry;
	fs_sb->unused_dentry_head = unused_dentry;
	return 0;
}

struct dentry* fetch_dentry_from_unused_list()
{
	if (fs_sb->unused_dentry_tail == NULL)    // not enough
		return NULL;
	struct unused_dentry *fetched_dentry = NULL;
	fetched_dentry = fs_sb->unused_dentry_tail;
	fs_sb->unused_dentry_tail = fetched_dentry->prev;
	fetched_dentry->prev->next = NULL;
	fetched_dentry->prev = NULL;
	struct dentry *dentry = NULL;
	dentry = fetched_dentry->dentry;
	free(fetched_dentry);
	fetched_dentry = NULL;
	return dentry;
}

// only remove from tail
int remove_dentry_from_unused_list(struct dentry *dentry)
{
	struct unused_dentry * unused_dentry = fs_sb->unused_dentry_tail;
	while (unused_dentry->dentry->fid != dentry->fid && unused_dentry != NULL) {
		unused_dentry = unused_dentry->prev;
	}
	if (unused_dentry == NULL) {
		printf("this dentry not in dirty list\n");
		return 0;
	}
	if (unused_dentry == fs_sb->unused_dentry_tail) {
		if (unused_dentry->prev == NULL) {
			fs_sb->unused_dentry_head = NULL;
			fs_sb->unused_dentry_tail = NULL;
			goto out;
		}
		fs_sb->unused_dentry_tail = unused_dentry->prev;
		unused_dentry->prev->next = NULL;
	} else {
		unused_dentry->next->prev = unused_dentry->prev;
		if (unused_dentry->prev != NULL) {
			unused_dentry->prev->next = unused_dentry->next;
		} else {
			fs_sb->unused_dentry_head = unused_dentry->next;
		}
	}
out:
	set_dentry_flag(dentry, D_dirty, 1);
	free(unused_dentry);
	unused_dentry = NULL;
	return 0;	
}

int charlen(char *str)
{
	int len = 0;
	while (str[len] != '\0') {
		len++;
	}
	return len;
}

bool is_prefix(const char *prefix, const char *str)
{
	bool result = true;
	int i;
	int len_prefix = strlen(prefix);
	int len_str = strlen(str);
	if (len_prefix >= len_str) {
		result = false;
	} else {
		for (i = 0; i < len_prefix; i++) {
			if (prefix[i] != str[i]) {
				result = false;
				break;
			}
		}
	}
	return result;
}

void init_sb(char * mount_point, char * access_point)
{
	fs_sb = (struct fs_super *) calloc(1, sizeof(struct fs_super));
	strcpy(fs_sb->alloc_path, access_point);
	if (mount_point[strlen(mount_point) - 1] == '/')
		mount_point[strlen(mount_point) - 1] = '\0';
	fs_sb->mount_point = mount_point;
	fs_sb->dirty_dentry_head = NULL;
	fs_sb->dirty_dentry_tail = NULL;
	fs_sb->unused_dentry_head = NULL;
	fs_sb->unused_dentry_tail = NULL;
	fs_sb->tree = RB_ROOT;
	fs_sb->curr_dir_id = 1;

	// root should in map first!
	struct stat root_buf;
	stat(access_point, &root_buf);
	struct dentry *dentry = (struct dentry *) calloc(1, sizeof(struct dentry));
	dentry->fid = 0;    // name in lustre
	//dentry->inode = root_buf.st_ino;
	//dentry->inode = generate_unique_id();
	dentry->inode = 1;    // root inode;
	dentry->flags = 0;
	set_dentry_flag(dentry, D_type, DIR_DENTRY);
	dentry->mode = root_buf.st_mode;
	dentry->ctime = root_buf.st_ctime;
	dentry->mtime = root_buf.st_mtime;
	dentry->atime = root_buf.st_atime;
	dentry->size = root_buf.st_size;
	dentry->uid = root_buf.st_uid;
	dentry->gid = root_buf.st_gid;
	dentry->nlink = root_buf.st_nlink;
	add_dentry_to_dirty_list(dentry);

	// put in map
	uint64_t addr = (uint64_t) dentry;
	put(&(fs_sb->tree), "0#/", addr);
}

int path_lookup(const char *path, struct lookup_res *lkup_res)
{
	int s, len = strlen(path);
	int last_pos = 0;
	int cur_inode = 0;
	uint64_t addr = 0;
	map_t *map_item = NULL;
	struct dentry *find_dentry = NULL;
	char dentry_name[DENTRY_NAME_SIZE];
	char search_key[MAP_KEY_LEN];
	

	s = 0;
	last_pos = 0;
	while (s <= len) {
		if (s == len || path[s] == '/') {
			memset(dentry_name, '\0', MAP_KEY_LEN);
			memset(search_key, '\0', MAP_KEY_LEN);
			s = s ? s : 1;
			memcpy(dentry_name, &path[last_pos], s - last_pos);
			sprintf(search_key, "%d", cur_inode);
			strcat(search_key, MAP_KEY_DELIMIT);
			strcat(search_key, dentry_name);
		#ifdef FS_DEBUG
			printf("path_lookup, dentry name = %s, search key = %s\n", dentry_name, search_key);
		#endif
			map_item = get(&(fs_sb->tree), search_key);
			if (map_item == NULL) {

			#ifdef FS_DEBUG
				printf("path_lookup, not find key = %s item in the map\n", search_key);
			#endif

				lkup_res->dentry = find_dentry;    // if failed record the last searched dentry
				//lkup_res->p_inode = cur_inode;
				if (s == len) {
					lkup_res->error = MISS_FILE;
				} else {
					lkup_res->error = MISS_DIR;
				}
				return ERROR;
			}
			lkup_res->p_inode = cur_inode;
			addr = map_item->val;
			find_dentry = (struct dentry *) addr;
			cur_inode = (int) find_dentry->inode;
			if (s == 1)
				last_pos = s;
			else
				last_pos = s + 1;
		}
		s++;
	}
	lkup_res->dentry = find_dentry;
	lkup_res->error = LOOKUP_SUCCESS;
	return SUCCESS;
}

void fs_init(char * mount_point, char * access_point)
{
	init_sb(mount_point, access_point);

	char create_path[PATH_LEN];
	memset(create_path, '\0', PATH_LEN);
	strcpy(create_path, fs_sb->alloc_path);
	strcat(create_path, "/");
	strcat(create_path, ALLOCATED_PATH);    // // like /mnt/lustre/pre_alloc

	if (access(create_path, F_OK) != 0) {
		mkdir(create_path, O_CREAT);
	}
	
	uint32_t i = 0;
	char part[8];
	int fd = 0;
	struct stat buf;
	struct dentry *dentry = NULL;
	int max_open_num = PRE_LOC_NUM;
	
	for (i = 1; i < max_open_num; i++) {
		dentry = (struct dentry *) calloc(1, sizeof(struct dentry));
		memset(part, '\0', 8);
		sprintf(part, "%d", i);
		strcat(create_path, "/");
		strcat(create_path, part);
		create_path[charlen(create_path)] = '\0';
		fd = open(create_path, O_CREAT | O_RDWR);
	#ifdef FS_DEBUG
		printf("fs_init, create_path = %s, open fd = %d\n", create_path, fd);
	#endif
		if (unlikely(fd < 0)) {
			printf("Init... part = %s not be created, max num increase\n", part);
			max_open_num++;
			continue;
			
		}
		stat(create_path, &buf);
		// hook dentry
		//dentry->fid = i;    // name in lustre
		dentry->fid = (uint32_t) fd;
		dentry->inode = buf.st_ino;
		dentry->flags = 0;
		dentry->mode = buf.st_mode;
		dentry->ctime = buf.st_ctime;
		dentry->mtime = buf.st_mtime;
		dentry->atime = buf.st_atime;
		dentry->size = buf.st_size;
		dentry->uid = buf.st_uid;
		dentry->gid = buf.st_gid;
		dentry->nlink = buf.st_nlink;

		add_dentry_to_unused_list(dentry);

		// prealloc for next
		dirname(create_path);
		//close(fd);
	}
}

int fs_open(const char *path, struct fuse_file_info *fileInfo)
{
	int ret = 0;
	struct dentry *dentry = NULL;
	struct lookup_res *lkup_res = NULL;
	lkup_res = (struct lookup_res *) malloc(sizeof(struct lookup_res));
	ret = path_lookup(path, lkup_res);
	if (lkup_res->error == MISS_DIR) {
		ret = -ENOENT;
		goto out;
	}
	dentry = lkup_res->dentry;
	if (lkup_res->error == MISS_FILE) {
		if ((fileInfo->flags & O_CREAT) == 0) {
			ret = -ENOENT;
			goto out;
		}
		if (get_dentry_flag(dentry, D_type) != DIR_DENTRY) {
			ret = -ENOTDIR;
			goto out;
		}
		char cur_name[DENTRY_NAME_SIZE];
		int len = strlen(path);
		int i = 0;
		int j = 0;
		for (i = len - 1; i >= 0; --i) {
			if (path[i] == '/')
				break;
		}
		for (i = i + 1, j = 0; i < len; i++, j++) {
			cur_name[j] = path[i];
		}
		cur_name[j] = '\0';

		uint32_t p_inode = dentry->inode;
		struct dentry *create_dentry = NULL;
		create_dentry = fetch_dentry_from_unused_list();
		if (create_dentry == NULL) {
			ret = -ENFILE;    // not enough, need pre-alloc
			goto out;
		}
	#ifdef FS_DEBUG
		printf("fs_open(create), fetch dentry fid = %d, inode = %d\n", (int)create_dentry->fid, (int)create_dentry->inode);
	#endif
		add_dentry_to_dirty_list(create_dentry);
		set_dentry_flag(create_dentry, D_type, FILE_DENTRY);
		create_dentry->mode = S_IFREG | 0644;
		// init the new dentry...
		char create_key[MAP_KEY_LEN];
		memset(create_key, '\0', MAP_KEY_LEN);
		sprintf(create_key, "%d", (int)p_inode);
		strcat(create_key, MAP_KEY_DELIMIT);
		strcat(create_key, cur_name);
		//create_key[charlen(create_key)] = '\0';
		uint64_t addr = (uint64_t) create_dentry;
		ret = put(&(fs_sb->tree), create_key, addr);
	#ifdef FS_DEBUG
		if (ret == 1) {
			printf("fs_open(create), put key = %s, its parent dentry inode = %d in the map!\n", create_key, (int)p_inode);
		} else {
			printf("fs_open(create), this key = %s, with parent inode = %d has already in the map\n", create_key, (int)p_inode);
		}
	#endif
		ret = SUCCESS;
		fileInfo->fh = addr;
		goto out;
	}
	ret = SUCCESS;
	fileInfo->fh = (uint64_t) dentry;
out:
	free(lkup_res);
	lkup_res = NULL;
	return ret;
}

int fs_create(const char * path, mode_t mode, struct fuse_file_info * fileInfo)
{
	int ret = 0;
	int len = strlen(path);
	int split_pos = 0;
	int i;
	for (i = len - 1; i >= 0; --i) {
		if (path[i] == '/')
			break;
	}
	split_pos = (i > 0) ? i : 1;
	char cur_name[DENTRY_NAME_SIZE];
	memset(cur_name, '\0', DENTRY_NAME_SIZE);
	if (split_pos == 1)
		memcpy(cur_name, &path[split_pos], len - split_pos);
	else
		memcpy(cur_name, &path[split_pos + 1], len - split_pos - 1);

	if (fileInfo != NULL)
		fileInfo->flags |= O_CREAT;
#ifdef FS_DEBUG
	printf("fs_create, will create path = %s, cur_name = %s\n", path, cur_name);
#endif
	struct lookup_res *lkup_res = NULL;
	lkup_res = (struct lookup_res *)malloc(sizeof(struct lookup_res));
	ret = path_lookup(path, lkup_res);
	if (ret == SUCCESS) {
		ret = -EEXIST;
		goto out;
	}
	if (lkup_res->error == MISS_DIR) {
		ret = -ENOENT;
		goto out;
	}
	if (get_dentry_flag(lkup_res->dentry, D_type) != DIR_DENTRY) {
		ret = -ENOTDIR;
		goto out;
	}
	uint32_t p_inode = lkup_res->dentry->inode;
	struct dentry *create_dentry = NULL;
	create_dentry = fetch_dentry_from_unused_list();
	if (create_dentry == NULL) {
		ret = -ENFILE;    // not enough, need pre-alloc
		goto out;
	}
#ifdef FS_DEBUG
	printf("fs_create, fetch dentry fid = %d, inode = %d\n", (int)create_dentry->fid, (int)create_dentry->inode);
#endif
	add_dentry_to_dirty_list(create_dentry);
	set_dentry_flag(create_dentry, D_type, FILE_DENTRY);
	create_dentry->mode = S_IFREG | 0644;
	// init the new dentry...
	char create_key[MAP_KEY_LEN];
	memset(create_key, '\0', MAP_KEY_LEN);
	sprintf(create_key, "%d", (int)p_inode);
	strcat(create_key, MAP_KEY_DELIMIT);
	strcat(create_key, cur_name);
	//create_key[charlen(create_key)] = '\0';
	uint64_t addr = (uint64_t) create_dentry;
	ret = put(&(fs_sb->tree), create_key, addr);
#ifdef FS_DEBUG
	if (ret == 1) {
		printf("fs_create, put key = %s, its parent dentry inode = %d in the map!\n", create_key, (int)p_inode);
	} else {
		printf("fs_create, this key = %s, with parent inode = %d has already in the map\n", create_key, (int)p_inode);
	}
#endif

	ret = SUCCESS;
	if (fileInfo != NULL)
		fileInfo->fh = addr;
out:
	free(lkup_res);
	lkup_res = NULL;
	return ret;
}

int fs_mkdir(const char *path, mode_t mode)
{
	int i;
	int ret = 0;
	int len = strlen(path);
	int split_pos = 0;
	for (i = len - 1; i >= 0; --i)
	{
		if (path[i] == '/')
			break;
	}
	split_pos = (i > 0) ? i : 1;

	char cur_name[DENTRY_NAME_SIZE];
	memset(cur_name, '\0', DENTRY_NAME_SIZE);
	if (split_pos == 1)
		memcpy(cur_name, &path[split_pos], len - split_pos);
	else
		memcpy(cur_name, &path[split_pos + 1], len - split_pos - 1);

#ifdef FS_DEBUG
	printf("fs_mkdir, will mkdir path = %s, cur_name = %s\n", path, cur_name);
#endif
	struct dentry *dentry = NULL;
	struct lookup_res *lkup_res = NULL;
	lkup_res = (struct lookup_res *)malloc(sizeof(struct lookup_res));
	ret = path_lookup(path, lkup_res);
	if (ret == SUCCESS) {
		ret = -EEXIST;
		goto out;
	}
	if (lkup_res->error == MISS_DIR) {
		ret = -ENOENT;
		goto out;
	}
	dentry = lkup_res->dentry;
	if (get_dentry_flag(dentry, D_type) != DIR_DENTRY) {
		ret = -ENOTDIR;
		goto out;
	}

	uint32_t p_inode = dentry->inode;
	struct dentry *mkdir_dentry = NULL;
	//mkdir_dentry = fetch_dentry_from_unused_list();
	// for dir, should generate the new dentry
	mkdir_dentry = (struct dentry *) calloc(1, sizeof(struct dentry));
	mkdir_dentry->fid = 0;
	mkdir_dentry->inode = generate_unique_id();
	mkdir_dentry->flags = 0;
	set_dentry_flag(mkdir_dentry, D_type, DIR_DENTRY);
	mkdir_dentry->mode = S_IFDIR | 0755;
	mkdir_dentry->ctime = time(NULL);
	mkdir_dentry->mtime = time(NULL);
	mkdir_dentry->atime = time(NULL);
	mkdir_dentry->size = 0;
	mkdir_dentry->uid = getuid();
	mkdir_dentry->gid = getgid();
	mkdir_dentry->nlink = 0;
#ifdef FS_DEBUG
	printf("fs_mkdir, create new dir dentry id = %d, name = %s\n", (int)mkdir_dentry->inode, cur_name);
#endif
	add_dentry_to_dirty_list(mkdir_dentry);	
	// init the new dentry...
	char mkdir_key[MAP_KEY_LEN];
	memset(mkdir_key, '\0', MAP_KEY_LEN);
	sprintf(mkdir_key, "%d", (int)p_inode);
	strcat(mkdir_key, MAP_KEY_DELIMIT);
	strcat(mkdir_key, cur_name);
	uint64_t addr = (uint64_t) mkdir_dentry;
	ret = put(&(fs_sb->tree), mkdir_key, addr);
#ifdef FS_DEBUG
	if (ret == 1) {
		printf("fs_mkdir, put key = %s, its parent dentry inode = %d in the map!\n", mkdir_key, (int)p_inode);
	} else {
		printf("fs_mkdir, this key = %s, with parent inode = %d has already in the map\n", mkdir_key, (int)p_inode);
	}
#endif

	ret = SUCCESS;
out:
	free(lkup_res);
	lkup_res = NULL;
	return ret;	
}

int fs_opendir(const char *path, struct fuse_file_info *fileInfo)
{
	int ret = 0;
	struct lookup_res *lkup_res = NULL;
	lkup_res = (struct lookup_res *)malloc(sizeof(struct lookup_res));
	ret = path_lookup(path, lkup_res);
	if (ret == ERROR) {
		ret = -ENOENT;
		goto out;
	} else {
		if (get_dentry_flag(lkup_res->dentry, D_type) != DIR_DENTRY) {
			ret = -ENOTDIR;
			goto out;
		}
		fileInfo->fh = (uint64_t) lkup_res->dentry;
	#ifdef FS_DEBUG
		printf("fs_opendir, open path = %s, inode = %ld\n", path, fileInfo->fh);
	#endif
	}
out:
	free(lkup_res);
	lkup_res = NULL;
	return ret;
}

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fileInfo)
{
	int i = 0;
	int j = 0;
	uint64_t addr = fileInfo->fh;
	struct dentry *p_dentry = NULL;
	p_dentry = (struct dentry *) addr;
	if (p_dentry == NULL)
		return ERROR;

	if (filler(buf, ".", NULL, 0) < 0) {
		printf("filler . error in func = %s\n", __FUNCTION__);
		return ERROR;
	}
	if (filler(buf, "..", NULL, 0) < 0) {
		printf("filler . error in func = %s\n", __FUNCTION__);
		return ERROR;
	}

	char matched_name[DENTRY_NAME_SIZE];
	char pre_key[MAP_PRE_KEY_LEN];
	memset(matched_name, '\0', DENTRY_NAME_SIZE);
	memset(pre_key, '\0', MAP_PRE_KEY_LEN);
	sprintf(pre_key, "%d", (int)p_dentry->inode);
	strcat(pre_key, MAP_KEY_DELIMIT);
	int len = charlen(pre_key);
	//pre_key[len] = '\0';

#ifdef FS_DEBUG
	printf("fs_readdir, readdir path = %s, pre_key = %s\n", path, pre_key);
#endif

	map_t *node;
	char *key = NULL;
	bool match = true;
	for (node = map_first(&(fs_sb->tree)); node; node = map_next(&(node->node))) {
		key = node->key;
		for (i = 0; i < len; i++) {
			if (pre_key[i] != key[i]) {
				match = false;
				break;
			}
		}
		if (match) {
			for (i = len, j = 0; i < strlen(key); i++, j++) {
				matched_name[j] = key[i];
			}
			matched_name[j] = '\0';
			if (filler(buf, matched_name, NULL, 0) < 0) {
				printf("filler %s error in func = %s\n", matched_name, __FUNCTION__);
				return ERROR;
			}
		} else {
			match = true;
		}
	}
	return SUCCESS;
}

int fs_getattr(const char* path, struct stat* st)
{
	int ret = 0;
	struct dentry *dentry = NULL;
	struct lookup_res *lkup_res = NULL;
	lkup_res = (struct lookup_res *)malloc(sizeof(struct lookup_res));
	ret = path_lookup(path, lkup_res);
	if (ret == ERROR) {
		ret = -ENOENT;
		goto out;
	}
	dentry = lkup_res->dentry;
#ifdef FS_DEBUG
	printf("fs_getattr, getattr path = %s, its inode = %d\n", path, (int)dentry->inode);
#endif
	if (strcmp(path, "/") == 0) {
		st->st_mode = S_IFDIR | 0755;
	} else {
		st->st_mode = dentry->mode;
	}
	// copy some parements from dentry to st
	st->st_nlink = dentry->nlink;
	st->st_size = dentry->size;
	st->st_ctime = dentry->ctime;

	st->st_uid = dentry->uid;
	st->st_gid = dentry->gid;
	st->st_atime = dentry->atime;
	st->st_mtime = dentry->mtime;
	dentry->atime = time(NULL);
out:
	free(lkup_res);
	lkup_res = NULL;
	return ret;
}

int fs_rmdir(const char *path)
{
	int ret = 0;
	int len = strlen(path);
	int split_pos = 0;
	int i;
	for (i = len - 1; i >= 0; --i) {
		if (path[i] == '/')
			break;
	}
	split_pos = (i > 0) ? i : 1;
	char *p_path = (char *)calloc(1, split_pos + 1);
	if (i < 0)
		return -ENOENT;
	int j;
	for (j = 0; j < split_pos; ++j) {
		p_path[j] = path[j];
	}
	p_path[j] = '\0';
	char cur_name[DENTRY_NAME_SIZE];
	memset(cur_name, '\0', DENTRY_NAME_SIZE);
	if (split_pos == 1)
		memcpy(cur_name, &path[split_pos], len - split_pos);
	else
		memcpy(cur_name, &path[split_pos + 1], len - split_pos - 1);

	struct dentry *dentry = NULL;
	struct lookup_res *lkup_res = NULL;
	lkup_res = (struct lookup_res *)malloc(sizeof(struct lookup_res));
	ret = path_lookup(p_path, lkup_res);
	if (ret == ERROR) {
		ret = -ENOENT;
		goto out;
	}
	dentry = lkup_res->dentry;
	if (get_dentry_flag(dentry, D_type) != DIR_DENTRY) {
		ret = -ENOTDIR;
		goto out;
	}

	char rm_key[MAP_KEY_LEN];
	memset(rm_key, '\0', MAP_KEY_LEN);
	sprintf(rm_key, "%d", (int)dentry->inode);    // parent inode (by generate_unique_id)
	strcat(rm_key, MAP_KEY_DELIMIT);
	strcat(rm_key, cur_name);
	map_t *rm_node;
	rm_node = get(&(fs_sb->tree), rm_key);
	if (rm_node == NULL) {
		ret = -ENOENT;
		goto out;
	}
	dentry = (struct dentry *) rm_node->val;
	if (get_dentry_flag(dentry, D_type) != DIR_DENTRY) {
		ret = -ENOTDIR;
		goto out;
	}
	
	char pre_key[MAP_PRE_KEY_LEN];
	memset(pre_key, '\0', MAP_PRE_KEY_LEN);
	sprintf(pre_key, "%d", (int)dentry->inode);
	strcat(pre_key, MAP_KEY_DELIMIT);
	len = charlen(pre_key);

#ifdef FS_DEBUG
	printf("fs_rmdir, check dir = %s whether have child, pre key = %s\n", cur_name, pre_key);
#endif
	map_t *node;
	char *key = NULL;
	bool match = true;
	// if you do not check the child, you can rm all the subtree
	for (node = map_first(&(fs_sb->tree)); node; node = map_next(&(node->node))) {
		key = node->key;
		for (i = 0; i < len; i++) {
			if (pre_key[i] != key[i]) {
				match = false;
				break;
			}
		}
		if (match) {
		#ifdef FS_DEBUG
			printf("fs_rmdir, find sub dir with key = %s\n", key);
		#endif
			ret = -ENOTEMPTY;
			goto out;
		}
		match = true;
	}

#ifdef FS_DEBUG
	printf("fs_rmdir, will del node and free dir dentry, key = %s\n", rm_key);
#endif
	del(&(fs_sb->tree), rm_node);
	remove_dentry_from_dirty_list(dentry);
	free(dentry);
	dentry = NULL;
	ret = SUCCESS;
out:
	free(lkup_res);
	lkup_res = NULL;
	return ret;
}

int changename(struct lookup_res *lkup_res, const char *path, const char *newpath)
{
	int p_inode = lkup_res->p_inode;
	int i, j;
	char cur_name[DENTRY_NAME_SIZE];
	char new_cur_name[DENTRY_NAME_SIZE];
	memset(cur_name, '\0', DENTRY_NAME_SIZE);
	memset(new_cur_name, '\0', DENTRY_NAME_SIZE);
	int len = strlen(path);
	int new_len = strlen(newpath);
	for (i = len - 1; i >= 0; i--) {
		if (path[i] == '/')
			break;
	}
	if (i == 0)
		memcpy(cur_name, &path[1], len);
	else
		memcpy(cur_name, &path[i + 1], len - i - 1);
	for (j = new_len - 1; j >= 0; j--) {
		if (newpath[j] == '/')
			break;
	}
	if (j == 0)
		memcpy(new_cur_name, &newpath[1], new_len);
	else
		memcpy(new_cur_name, &newpath[j + 1], new_len - j - 1);

	char old_key[MAP_KEY_LEN];
	char new_key[MAP_KEY_LEN];
	memset(old_key, '\0', MAP_KEY_LEN);
	memset(new_key, '\0', MAP_KEY_LEN);
	sprintf(old_key, "%d", p_inode);
	strcat(old_key, MAP_KEY_DELIMIT);
	strcat(old_key, cur_name);
	sprintf(new_key, "%d", p_inode);
	strcat(new_key, MAP_KEY_DELIMIT);
	strcat(new_key, new_cur_name);
	uint64_t addr = (uint64_t) lkup_res->dentry;

#ifdef FS_DEBUG
	printf("changename, old_key = %s, new_key = %s\n", old_key, new_key);
#endif
	map_t *rm_node;
	rm_node = get(&(fs_sb->tree), old_key);
	if (rm_node == NULL)
		return -1;
	del(&(fs_sb->tree), rm_node);
	put(&(fs_sb->tree), new_key, addr);
	return 0;
}

// mv /a/a /b  ==> rename /a/a /b/a
int fs_rename(const char *path, const char *newpath)
{
	int i, j, ret = 0;
	int len_path = strlen(path);
	int len_newpath = strlen(newpath);
	struct lookup_res *lkup_res = NULL;
	struct lookup_res *new_lkup_res = NULL;
	if (strcmp(path, newpath) == 0)
		return 0;
	lkup_res = (struct lookup_res *)malloc(sizeof(struct lookup_res));
	new_lkup_res = (struct lookup_res *)malloc(sizeof(struct lookup_res));
	ret = path_lookup(path, lkup_res);
	if (ret == ERROR) {
		ret = -ENOENT;
		goto out;
	}
	ret = path_lookup(newpath, new_lkup_res);
	if (ret == SUCCESS) {
		ret = -EEXIST;
		goto out;
	}
	if (new_lkup_res->error == MISS_DIR) {
		ret = -ENOENT;
		goto out;
	}
#ifdef FS_DEBUG
	printf("fs_rename, path = %s, newpath = %s\n", path, newpath);
#endif
	bool ismove = false;
	for (i = len_path - 1; i >= 0; i--) {
		if (path[i] == '/')
			break;
	}
	for (j = len_newpath - 1; j >= 0; j--) {
		if (newpath[j] == '/')
			break;
	}
	if (i != j) {
		ismove = true;
	} else {
		int k = i;
		for (; k >= 0; k--) {
			if (path[k] != newpath[k]) {
				ismove = true;
				break;
			}
		}
		if (i < j) {
			ret = -EPERM;
			goto out;
		}
	}
	if (ismove && get_dentry_flag(new_lkup_res->dentry, D_type) != DIR_DENTRY) {
		ret = -ENOTDIR;
		goto out;
	}
#ifdef FS_DEBUG
	printf("fs_rename, ismove = %d\n", ismove);
#endif
	if (ismove)
		//ret = movename();
		ret = -ENOSYS;
	else
		ret = changename(lkup_res, path, newpath);
out:
	free(lkup_res);
	free(new_lkup_res);
	lkup_res = NULL;
	new_lkup_res = NULL;
	return ret;
}

int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo)
{
	int ret = 0;
	uint64_t addr = fileInfo->fh;
	struct dentry *dentry = NULL;
	dentry = (struct dentry *) addr;
	int fd = (int)dentry->fid;

	if (unlikely(fd < 0)) {
		return -EBADF;
	}
	ret = pread(fd, buf, size, offset);
#ifdef FS_DEBUG
	printf("fs_read, read %d data from fd = %d in path = %s\n", ret, fd, path);
#endif
	offset += size;
	return ret;
}

int fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo)
{
	int ret = 0;
	uint64_t addr = fileInfo->fh;
	struct dentry *dentry = NULL;
	dentry = (struct dentry *) addr;
	int fd = (int)dentry->fid;

	if (unlikely(fd < 0)) {
		return -EBADF;
	}
	ret = pwrite(fd, buf, size, offset);
#ifdef FS_DEBUG
	printf("fs_write, write %d data from fd = %d in path = %s\n", ret, fd, path);
#endif
	dentry->size += size;
	offset += size;
	return ret;
}

int fs_release(const char *path, struct fuse_file_info *fileInfo)
{
#ifdef FS_DEBUG
	printf("fs_release, path = %s has been closed\n", path);
#endif
	return 0;
}

int fs_releasedir(const char * path, struct fuse_file_info * info)
{
	info->fh = -1;
	return SUCCESS;
}

int fs_utimens(const char * path, const struct timespec tv[2])
{
	int ret = 0;
	struct dentry *dentry = NULL;
	struct lookup_res *lkup_res = NULL;
	lkup_res = (struct lookup_res *)malloc(sizeof(struct lookup_res));
	ret = path_lookup(path, lkup_res);
	if (unlikely(ret == ERROR)) {
		ret = -ENOENT;
		goto out;
	}
	dentry = lkup_res->dentry;
	dentry->atime = tv[0].tv_sec;
	dentry->mtime = tv[1].tv_sec;
#ifdef FS_DEBUG
	printf("fs_utimens, update time dentry inode = %d\n", (int)dentry->inode);
#endif
out:
	free(lkup_res);
	lkup_res = NULL;
	return ret;
}

int fs_truncate(const char * path, off_t length)
{
	return -ENOSYS;
}

int fs_unlink(const char * path)
{
	int ret = 0;
	int len = strlen(path);
	int split_pos = 0;
	int i;
	for (i = len - 1; i >= 0; --i) {
		if (path[i] == '/')
			break;
	}
	split_pos = (i > 0) ? i : 1;
	char *p_path = (char *)calloc(1, split_pos + 1);
	if (i < 0)
		return -ENOENT;
	int j;
	for (j = 0; j < split_pos; ++j) {
		p_path[j] = path[j];
	}
	p_path[j] = '\0';
	char cur_name[DENTRY_NAME_SIZE];
	memset(cur_name, '\0', DENTRY_NAME_SIZE);
	if (split_pos == 1)
		memcpy(cur_name, &path[split_pos], len - split_pos);
	else
		memcpy(cur_name, &path[split_pos + 1], len - split_pos - 1);

	struct dentry *dentry = NULL;
	struct lookup_res *lkup_res = NULL;
	lkup_res = (struct lookup_res *)malloc(sizeof(struct lookup_res));
	ret = path_lookup(p_path, lkup_res);
	if (ret == ERROR) {
		ret = -ENOENT;
		goto out;
	}
	dentry = lkup_res->dentry;
	if (get_dentry_flag(dentry, D_type) != DIR_DENTRY) {
		ret = -ENOTDIR;
		goto out;
	}

	char rm_key[MAP_KEY_LEN];
	memset(rm_key, '\0', MAP_KEY_LEN);
	sprintf(rm_key, "%d", (int)dentry->inode);    // parent inode
	strcat(rm_key, MAP_KEY_DELIMIT);
	strcat(rm_key, cur_name);
	map_t *rm_node;
	rm_node = get(&(fs_sb->tree), rm_key);
	if (rm_node == NULL) {
		ret = -ENOENT;
		goto out;
	}
	dentry = (struct dentry *) rm_node->val;
	if (get_dentry_flag(dentry, D_type) == DIR_DENTRY) {
		ret = -EISDIR;
		goto out;
	}

#ifdef FS_DEBUG
	printf("fs_unlink, will del node and add a unused dentry, key = %s\n", rm_key);
#endif
	del(&(fs_sb->tree), rm_node);
	remove_dentry_from_dirty_list(dentry);
	if ((dentry->flags & S_IFLNK) == 1) {
		free(dentry);
		dentry = NULL;
		ret = SUCCESS;
		goto out;
	}
	// change the dentry
	ftruncate(dentry->fid, 0);    // delete the file
	dentry->size = 0;
	dentry->flags = 0;
	set_dentry_flag(dentry, D_type, FILE_DENTRY);
	dentry->nlink = 0;
	dentry->mtime = time(NULL);
	add_dentry_to_unused_list(dentry);    // for file, need recycle
	ret = SUCCESS;
out:
	free(lkup_res);
	lkup_res = NULL;
	return ret;
}

int fs_chmod(const char * path, mode_t mode)
{
	return -ENOSYS;
}

int fs_chown(const char * path, uid_t owner, gid_t group)
{
	return -ENOSYS;
}

int fs_access(const char * path, int amode)
{
#ifdef FS_DEBUG
	printf("fs_access path = %s, mast = %08x\n", path, amode);
#endif
	return 0;
}

int fs_symlink(const char * oldpath, const char * newpath)
{
	int ret = 0;
	int i, j;
	bool isprefix = true;
	isprefix = is_prefix(fs_sb->mount_point, oldpath);
	int len_oldpath = strlen(oldpath);
	int len_mount = strlen(fs_sb->mount_point);
	struct lookup_res *lkup_res = NULL;
	struct lookup_res *old_lkup_res = NULL;
	lkup_res = (struct lookup_res *)malloc(sizeof(struct lookup_res));
	old_lkup_res = (struct lookup_res *)malloc(sizeof(struct lookup_res));
	ret = path_lookup(newpath, lkup_res);
	if (lkup_res->error != MISS_FILE) {
		ret = -EEXIST;
		goto out;
	}
	if (get_dentry_flag(lkup_res->dentry, D_type) != DIR_DENTRY) {
		ret = -ENOENT;
		goto out;
	}
	int len = 0;
	if (isprefix)
		len = len_oldpath - len_mount;
	else
		len = len_oldpath;
	char* old_real_path = (char *)calloc(1, len + 1);
	if (isprefix) {
		for (i = len_mount, j = 0; i < len_oldpath; i++, j++) {
			old_real_path[j] = oldpath[i];
		}
	} else {
		for (j = 0; j < len_oldpath; j++) {
			old_real_path[j] = oldpath[j];
		}
	}
	old_real_path[j] = '\0';
#ifdef FS_DEBUG
	printf("fs_symlink, oldpath = %s, old_real_path = %s, is prefix = %d\n", oldpath, old_real_path, isprefix);
#endif
	ret = path_lookup(old_real_path, old_lkup_res);
	if (ret == ERROR) {
		ret = -ENOENT;
		goto out;
	}
	char cur_name[DENTRY_NAME_SIZE];
	memset(cur_name, '\0', DENTRY_NAME_SIZE);
	int split_pos = 0;
	for (i = strlen(newpath) - 1; i >= 0; --i) {
		if (newpath[i] == '/')
			break;
	}
	split_pos = (i > 0) ? i : 1;
	if (split_pos == 1)
		memcpy(cur_name, &newpath[split_pos], strlen(newpath) - split_pos);
	else
		memcpy(cur_name, &newpath[split_pos + 1], strlen(newpath) - split_pos - 1);
	uint32_t p_inode = lkup_res->dentry->inode;
	struct dentry *create_dentry = NULL;
	create_dentry = (struct dentry *)calloc(1, sizeof(struct dentry));
	create_dentry->fid = old_lkup_res->dentry->fid;
	create_dentry->inode = old_lkup_res->dentry->inode;
	create_dentry->flags = 0;
	add_dentry_to_dirty_list(create_dentry);
	set_dentry_flag(create_dentry, D_type, FILE_DENTRY);
	create_dentry->mode = S_IFLNK | 0777;
	create_dentry->ctime = time(NULL);
	create_dentry->mtime = time(NULL);
	create_dentry->atime = time(NULL);
	create_dentry->uid = getuid();
	create_dentry->gid = getgid();
	old_lkup_res->dentry->nlink++;
#ifdef FS_DEBUG
	printf("fs_symlink, new link file inode = %d, linked name = %s\n", create_dentry->inode, basename(old_real_path));
#endif

	char create_key[MAP_KEY_LEN];
	memset(create_key, '\0', MAP_KEY_LEN);
	sprintf(create_key, "%d", (int)p_inode);
	strcat(create_key, MAP_KEY_DELIMIT);
	strcat(create_key, cur_name);
	uint64_t addr = (uint64_t) create_dentry;
	ret = put(&(fs_sb->tree), create_key, addr);
#ifdef FS_DEBUG
	if (ret == 1) {
		printf("fs_create, put key = %s, its parent dentry inode = %d in the map!\n", create_key, (int)p_inode);
	} else {
		printf("fs_create, this key = %s, with parent inode = %d has already in the map\n", create_key, (int)p_inode);
	}
#endif
	ret = SUCCESS;
out:
	free(lkup_res);
	free(old_lkup_res);
	lkup_res = NULL;
	old_lkup_res = NULL;
	return ret;
}

int fs_readlink(const char * path, char * buf, size_t size)
{
	int ret = 0;
	struct dentry *dentry = NULL;
	struct lookup_res *lkup_res = NULL;
	lkup_res = (struct lookup_res *) malloc(sizeof(struct lookup_res));
	ret = path_lookup(path, lkup_res);
	if (ret == ERROR) {
		ret = -ENOENT;
		goto out;
	}
	dentry = lkup_res->dentry;
	if (!S_ISLNK(dentry->mode)) {
		ret = EINVAL;
		goto out;
	}

	map_t *node;
	struct dentry *find_dentry = NULL;
	char *key = NULL;
	int i, j;
	for (node = map_first(&(fs_sb->tree)); node; node = map_next(&(node->node))) {
		find_dentry = (struct dentry *)node->val;
		if (S_ISLNK(find_dentry->mode))
			continue;
		if (get_dentry_flag(find_dentry, D_type) == DIR_DENTRY) {
			if (find_dentry->inode == dentry->inode) {
			#ifdef FS_DEBUG
				printf("fs_readlink, find one is dir with inode = %d\n", (int)find_dentry->inode);
			#endif
				goto find_one;
			}
		} else if(get_dentry_flag(find_dentry, D_type) == FILE_DENTRY) {
			if (find_dentry->fid == dentry->fid) {
			#ifdef FS_DEBUG
				printf("fs_readlink, find one is file with fid = %d\n", (int) find_dentry->fid);
			#endif
				goto find_one;
			}
		}
	}
find_one:
	key = node->key;
#ifdef FS_DEBUG
	printf("fs_readlink, find key = %s\n", key);
#endif
	for (i = 0; i < strlen(key); i++) {
		if (key[i] == '#')
			break;
	}
	for (i = i + 1, j = 0; i < strlen(key); i++, j++) {
		buf[j] = key[i];
	}
	buf[j] = '\0';
	//sprintf(buf, "%d", (int)dentry->inode);
#ifdef FS_DEBUG
	printf("fs_readlink, buf = %s, dentry inode = %d\n", buf, (int)dentry->inode);
#endif
out:
	free(lkup_res);
	lkup_res = NULL;
	return ret;
}

int fs_destroy()
{

}