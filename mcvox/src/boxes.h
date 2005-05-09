#ifndef __BOXES_H
#define __BOXES_H

int     display_box      (WPanel *p, char **user, char **mini,
			  int *use_msformat, int num);
sortfn *sort_box         (sortfn *sort_fn, int *reverse,
			  int *case_sensitive);
void    confirm_box      (void);
void    display_bits_box (void);
void    configure_vfs    (void);
void    jobs_cmd         (void);
char   *cd_dialog        (void);
void    symlink_dialog   (char *existing, char *new,
			  char **ret_existing, char **ret_new);
char   *tree_box         (char *current_dir);

#endif
