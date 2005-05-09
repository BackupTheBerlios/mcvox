#ifndef __PROFILE_H
#define __PROFILE_H
/* Prototypes for the profile management functions */

short GetPrivateProfileString (const char * AppName, char * KeyName,
			       char * Default, char * ReturnedString,
			       short Size, char * FileName);

int GetProfileString (const char * AppName, char * KeyName, char * Default, 
		      char * ReturnedString, int Size);

int GetPrivateProfileInt (const char * AppName, char * KeyName, int Default,
			   char * File);

int GetProfileInt (const char * AppName, char * KeyName, int Default);

int WritePrivateProfileString (const char * AppName, char * KeyName, char * String,
				char * FileName);

int WriteProfileString (const char * AppName, char * KeyName, char * String);

void sync_profiles (void);

void free_profiles (void);
char *get_profile_string (const char *AppName, char *KeyName, char *Default,
			  char *FileName);

/* New profile functions */

/* Returns a pointer for iterating on appname section, on profile file */
void *profile_init_iterator (char *appname, char *file);

/* Returns both the key and the value of the current section. */
/* You pass the current iterating pointer and it returns the new pointer */
void *profile_iterator_next (void *s, char **key, char **value);

/* Removes all the definitions from section appname on file */
void profile_clean_section (char *appname, char *file);
int profile_has_section (char *section_name, char *profile);

/* Forgets about a .ini file, to disable updating of it */
void profile_forget_profile (char *file);

/* Removes information from a profile */
void free_profile_name (char *s);

#endif	/* __PROFILE_H */
