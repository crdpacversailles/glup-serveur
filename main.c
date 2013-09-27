#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <string.h>


#define NB_MAX_FILS 3
//l'id du jeu est la chaîne uniqid générée par le script web
#define LONGUEUR_MAX_ID_JEU 20
//le nom du jeu est la clé passé au compilateur pour la factory AS3
#define LONGUEUR_MAX_NOM_JEU 20
//taille du tableau de threads dédiés à la gestion des zombies
#define NB_THREADS_GESTION_ZOMBIES 20
//longueurs maximale d'un fichier de contenu
#define LONGUEUR_MAX_FICHIER_CONTENU 6000

extern void log_printf(int, const char*, ...);
extern void lireLigne(int , char *, int);
extern void gererFin(int);
extern void *gererZombies(pid_t *);
extern void clean( char *);
static void purger(void);

int socket_d_ecoute, socket_de_service;
int nb_fils;

//gestion par des threads des zombies
int nb_threads;
pthread_t threads[NB_THREADS_GESTION_ZOMBIES];
//pid des processus issus des 2 niveaux de fork
// niveau 1 : créé lors de la connexion d'un client
//niveau 2 : créé pour le recouvrement par compilation
// le père écoute les connexions
// le fils scrute le réperoire de sortie des swf
// le petit-fils compile
pid_t pid1, pid2;
socklen_t namelen;
struct sockaddr_in nom;
/*
* handler attaché à sigchild
* décrémente le nombre de fils
*/


int main()
{
/*
**************************Gestion du nombre de processus fils
*/
    //on attache le gestionnaire
    signal(SIGCHLD, gererFin);
    nb_fils=0;
    nb_threads=0;

    //lecture du cfichiers de params
    FILE *fichier=fopen("params.txt","r");
    if (fichier == NULL)
        {
        perror("fournissez un fichier params.txt");

        }
        //valeurs de retour inutilisées
        char*  ch;

    char racineExport[200];
    ch = fgets(racineExport, 200, fichier);
    clean(racineExport);

    char racineSources[200];
    ch = fgets(racineSources, 200, fichier);
    clean(racineSources);

    char racineSDK[200] ;
    ch= fgets(racineSDK, 200, fichier);;
    clean(racineSDK);
    char port[7] ;
    ch=fgets(port, 20, fichier);
    log_printf(LOG_INFO,"Démarrage sur le port : %s", port);
    log_printf(LOG_INFO,"Répertoire d'export des animations : %s", racineExport);
    log_printf(LOG_INFO,"Répertoire des sources à compiler : %s", racineSources);
    log_printf(LOG_INFO,"Répertoire du SDK : %s", racineSDK);
    fclose(fichier);

    //pour récupérer des valeurs de retour inutilisées
    int retour;
/*
**************************Création de la socket
*/
    socket_d_ecoute = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (socket_d_ecoute<0)
    {
     perror("pb socket");
     exit(1);
    }

    //initialisation de l'adresse d'application
    bzero(&nom, sizeof(nom));
    nom.sin_family=AF_INET;
    nom.sin_port = htons(atoi(port));
    nom.sin_addr.s_addr = INADDR_ANY;

    if(bind(socket_d_ecoute, &nom, sizeof(nom))<0) {
        perror("pb bind");
        exit(1);
    }

    if (listen(socket_d_ecoute, 3)<0) {
        perror("pb listen");
        exit(1);
    }
/*
**************************Boucle du serveur
*/
 for(;;) {


         socket_de_service=accept(socket_d_ecoute, &nom, &namelen);
        /*
        **************************Gestion d'un connexion client
        */
         if(nb_fils>=NB_MAX_FILS) {
             //on renvoie aussitôt un signal : patienter.
            retour= write(socket_de_service, "ko", 2);
         } else {
            pid1 =fork();

            if(pid1==0) {
                nb_fils++;
                //printf("[+1 fils --> %d]", nb_fils);
                if(socket_de_service<0) {
                          perror("pb connexion");
                }
/*
**************************Lecture des données sur la socket
*/
        /*
        L'identificateur du jeu
        */
        char uniqid[LONGUEUR_MAX_ID_JEU];
        lireLigne(socket_de_service, uniqid, LONGUEUR_MAX_ID_JEU);

        /*
        Le nom du jeu souhaité
        */
        char jeu[LONGUEUR_MAX_NOM_JEU];
        lireLigne(socket_de_service,jeu,LONGUEUR_MAX_NOM_JEU);

        /*
        La chaîne xml représentant le contenu
        */
        char contenu[LONGUEUR_MAX_FICHIER_CONTENU];
        lireLigne(socket_de_service,contenu, LONGUEUR_MAX_FICHIER_CONTENU);

        /*
        La chaîne xml représentant les options
        */
        char options[LONGUEUR_MAX_FICHIER_CONTENU];
        lireLigne(socket_de_service,options, LONGUEUR_MAX_FICHIER_CONTENU);

        /*
        * paramètres de hauteur et de largeur
        */
        char largeur[10];
        char hauteur[10];
        lireLigne(socket_de_service,largeur, 10);
        lireLigne(socket_de_service,hauteur, 10);

        char prefixeExportContenu[] = "/contenu_";
        char extensionXML[] = ".xml";
        char cheminExportContenu[strlen(racineExport)+strlen(prefixeExportContenu)+strlen(uniqid)+strlen(extensionXML)];
        sprintf(cheminExportContenu, "%s%s%s%s", racineExport, prefixeExportContenu,uniqid, extensionXML);
        int fichierContenu= open(cheminExportContenu, O_CREAT | O_RDWR,  S_IWUSR | S_IRUSR);
        log_printf(LOG_INFO,"Tentative d'ouverture d'un fichier de contenu : \n%s\n", cheminExportContenu);
        if (fichierContenu==-1) perror("pb création fichier");
        log_printf(LOG_INFO,"Tentative réussie\n");
        retour = write(fichierContenu, contenu, strlen(contenu));


         char prefixeExportOptions[] = "/options_";

        char cheminExportOptions[strlen(racineExport)+strlen(prefixeExportOptions)+strlen(uniqid)+strlen(extensionXML)];
        sprintf(cheminExportOptions, "%s%s%s%s", racineExport, prefixeExportOptions,uniqid, extensionXML);

        int fichierOptions= open(cheminExportOptions, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR);
        log_printf(LOG_INFO,"Tentative d'ouverture d'un fichier d'options : \n%s\n", cheminExportOptions);
        if (fichierOptions==-1) perror("pb création fichier");
        log_printf(LOG_INFO,"Tentative réussie\n");
        retour = write(fichierOptions, options, strlen(options));


        char debutArgOptionsXML[] = "-define=CONFIG::chemin_options, \"";
        char finArgXML[] = "\"";

        char argOptionsXML[strlen(debutArgOptionsXML)+strlen(cheminExportOptions)+strlen(finArgXML)];
        sprintf(argOptionsXML, "%s%s%s", debutArgOptionsXML, cheminExportOptions, finArgXML);

        char debutArgContenuXML[] = "-define=CONFIG::chemin_contenu, \"";
        char argContenuXML[strlen(debutArgContenuXML)+strlen(cheminExportContenu)+strlen(finArgXML)];
        sprintf(argContenuXML, "%s%s%s", debutArgContenuXML, cheminExportContenu, finArgXML);

        /*Le nom du fichier d'export*/
        char debutNomSwf[]="/jeu_";
        char extensionSWF[]=".swf";
        char nomSWF[strlen(debutNomSwf)+strlen(uniqid)+strlen(extensionSWF)];
        sprintf(nomSWF, "%s%s%s", debutNomSwf, uniqid, extensionSWF);

        char src[]="/src";
        char mainClass[]="/fr/acversailles/crdp/glup/framework/Main.as";
        char sdkLib[]="/frameworks/libs/";
        char frameworkLib[]="/lib";

        char argMainClass[strlen(racineSources)+strlen(src)+strlen(mainClass)];
        sprintf(argMainClass, "%s%s%s", racineSources, src, mainClass);

        char * argSourcePatha="-compiler.source-path";
        char argSourcePathb[strlen(racineSources)+strlen(src)];
        sprintf(argSourcePathb, "%s%s", racineSources, src);

        char * argLibraryPath1a="-compiler.library-path";
        char argLibraryPath1b[strlen(racineSDK)+strlen(sdkLib)];
        sprintf(argLibraryPath1b, "%s%s", racineSDK, sdkLib);

         char * argLibraryPath2a="-compiler.library-path";
         char argLibraryPath2b[strlen(racineSources)+strlen(frameworkLib)];
         sprintf(argLibraryPath2b, "%s%s", racineSources, frameworkLib);

         char * outPuta="-output";
         char outPutb[strlen(racineExport)+strlen(nomSWF)];
         sprintf(outPutb, "%s%s", racineExport, nomSWF);

        char * argTaillea="-default-size";
        char * argTailleb=largeur;
        char * argTaillec=hauteur;

        char * argCadencea="-default-frame-rate";
        char * argCadenceb="24";

        char * argSecurite="-use-network=true";
        char * argPlayer="-target-player=10.0.0";
        char * argCouleura="-default-background-color";
        char * argCouleurb="0xC0C0C0";
        char * argSLRSL="-static-link-runtime-shared-libraries=true";

        char debutArgNomJeu[] = "-define=CONFIG::nom,\"";
        char finArgNomJeu[] = "\"";
        char argNomJeu[strlen(debutArgNomJeu)+strlen(jeu)+strlen(finArgNomJeu)];
        sprintf(argNomJeu, "%s%s%s", debutArgNomJeu, jeu, finArgNomJeu);

        char debutArgLargeur[] = "-define=CONFIG::largeur,\"";
        char finArgLargeur[] = "\"";
        char argLargeur[strlen(debutArgLargeur)+strlen(largeur) +strlen(finArgLargeur)];
        sprintf(argLargeur, "%s%s%s", debutArgLargeur, largeur, finArgLargeur);

        char debutArgHauteur[] = "-define=CONFIG::hauteur,\"";
        char finArgHauteur[] = "\"";
        char argHauteur[strlen(debutArgHauteur)+strlen(hauteur) +strlen(finArgHauteur)];
        sprintf(argHauteur, "%s%s%s", debutArgHauteur, hauteur, finArgHauteur);

        pid2=fork();
                 /*
                ********Code du fils
                */
                 if(pid2!=0) {

                     //FILE * fichier=NULL;

//                     while(fichier==NULL) {
//                         fichier=fopen(outPutb, "r");
//                         sleep(0.1);
//                     }
                    int status;
                    waitpid(pid2, &status, 0);
                     log_printf(LOG_INFO,"[Suppression du petit-fils chargé de la compilation, de pid : %d\r\r]", pid2);
                    retour=write(socket_de_service, "ok", 2);
                    close(socket_de_service);
                    exit(0);
                 } //fin du code du petit fils
                 else {
                /*
                ********Code du petit fils
                */
                char finCheminMXMLC[]="/bin/mxmlc";
                char cheminMXMLC[strlen(racineSDK)+strlen(finCheminMXMLC)];
                sprintf(cheminMXMLC, "%s%s",racineSDK, finCheminMXMLC);
                    execl(cheminMXMLC,
                    "mxmlc",
                    argMainClass,
                    argSourcePatha,
                    argSourcePathb,
                    argLibraryPath1a,
                    argLibraryPath1b,
                    argLibraryPath2a,
                    argLibraryPath2b,
                    outPuta,
                    outPutb,
                    argTaillea,
                    argTailleb,
                    argTaillec,
                    argCadencea,
                    argCadenceb,
                    argSecurite,
                    argPlayer,
                    argCouleura,
                    argCouleurb,
                    argSLRSL,
                    argNomJeu,
                    argOptionsXML,
                    argContenuXML,
                    argLargeur,
                    argHauteur,
                    NULL);
                    exit(0);
                 }//fin du code du petit fils

        }//fin du code du fils et du petit fils
        else {
            nb_threads++;
            nb_threads=nb_threads%NB_THREADS_GESTION_ZOMBIES;
            log_printf(LOG_INFO,"[Nombre de threads pour gérer la fin des fils: %d\r\r]", nb_threads);
            log_printf(LOG_INFO,"[Pid du fils à supprimer : %d\r\r]", pid1);
            pthread_create(&threads[nb_threads], NULL, (void *) gererZombies, (void *) &pid1);
        }
    }//fin if else nb fils
   }//fin for (boucle du serveur)

    return 0;
}
void lireLigne(int fd, char *pt, int maxlong) {
 int n, rr;
 char c;
 for(n=1; n<maxlong; n++) {
     rr=read(fd, &c, 1);
     if (c=='$') {
         break;
     }
     else  *pt++=c;
 }
 *pt='\0';
}
void gererFin(int sig){
    nb_fils--;
    log_printf(LOG_INFO,"[Le nombre actuel de fils : %d\r\r]", nb_fils);
}
void *gererZombies(pid_t * pid){
    int status;
    int retval=0;
    log_printf(LOG_INFO,"On attend ce fils du serveur : %d\r]", *pid);
    waitpid(*pid, &status, 0);
    log_printf(LOG_INFO,"Suppression de ce fils du serveur : %d\r]", *pid);
    pthread_exit(&retval);
}
static void purger(void)
{
    int c;

    while ((c = getchar()) != '\n' && c != EOF)
    {}
}

void clean (char *chaine)
{
    char *p = strchr(chaine, '\n');

    if (p)
    {
        *p = 0;
    }

    else
    {
        purger();
    }
}

void log_printf(int priority, const char* format, ...)
{
        va_list args;
        char buffer[8192];
        unsigned int len;

        va_start(args, format);
        len = vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        if (len >= sizeof(buffer)) {
                if (format[strlen(format) - 1] == '\n')
                        buffer[sizeof(buffer) - 2] = '\n';
                buffer[sizeof(buffer) - 1] = '\0';
        }

        syslog(priority, "%s", buffer);

        //if (g_options.debug)
        fprintf(stdout, "%s\n", buffer);
}
