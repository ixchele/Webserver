#include <iostream>
#include <string>
#include <stdexcept>
#include <cstring> // Pour memset
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

// =====================================================================
// CLASSES RAII (C++98) : Pour ne jamais fuir de mémoire ou de file descriptors
// =====================================================================

// Classe pour gérer la fermeture du socket automatiquement
class Socket {
private:
    int fd;
    // En C++98, pour empêcher la copie, on rend le constructeur de copie privé
    Socket(const Socket&); 
    Socket& operator=(const Socket&);

public:
    Socket(int domain, int type, int protocol) {
        fd = socket(domain, type, protocol);
        if (fd < 0) throw std::runtime_error("Erreur de creation du socket");
    }
    ~Socket() { 
        if (fd >= 0) close(fd); 
    }
    int get() const { return fd; }
};

// Classe pour libérer la liste de getaddrinfo automatiquement
class AddrInfoGuard {
private:
    struct addrinfo* ptr;
    AddrInfoGuard(const AddrInfoGuard&);
    AddrInfoGuard& operator=(const AddrInfoGuard&);

public:
    explicit AddrInfoGuard(struct addrinfo* p) : ptr(p) {}
    ~AddrInfoGuard() { 
        if (ptr != NULL) freeaddrinfo(ptr); 
    }
};

// =====================================================================
// PROGRAMME PRINCIPAL
// =====================================================================

int main() {
    // PARTIE 1 : getaddrinfo et les "poupées russes"
    
    struct addrinfo hints;
    // En C++98, pas de {}, on doit utiliser memset pour tout mettre à zéro
    std::memset(&hints, 0, sizeof(hints)); 
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL; // Pas de nullptr en C++98, on utilise NULL
    
    int status = getaddrinfo("google.com", "80", &hints, &res);
    if (status != 0) {
        std::cerr << "Erreur getaddrinfo: " << gai_strerror(status) << '\n';
        return 1;
    }

    // On confie le pointeur à notre classe RAII pour s'assurer du nettoyage
    AddrInfoGuard guard(res);

    // CAST C++ : Pas de 'auto'. On explicite le type.
    struct sockaddr_in* ipv4 = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);

    char ipstr[INET6_ADDRSTRLEN];
    inet_ntop(res->ai_family, &(ipv4->sin_addr), ipstr, sizeof(ipstr));
    std::cout << "1. L'IP trouvee est : " << ipstr << '\n';

    // PARTIE 2 : Le cast "Magique" pour connect()
    
    try {
        Socket sock(AF_INET, SOCK_STREAM, 0);

        struct sockaddr_in mon_adresse;
        // Obligatoire en C++98 : on remplit la structure de zéros (y compris les 8 octets de sin_zero)
        std::memset(&mon_adresse, 0, sizeof(mon_adresse));
        
        mon_adresse.sin_family = AF_INET;
        mon_adresse.sin_port = htons(80);
        inet_pton(AF_INET, ipstr, &(mon_adresse.sin_addr));

        std::cout << "2. Tentative de connexion a " << ipstr << "...\n";

        // LE CAST MAGIQUE C++98
        struct sockaddr* ptr_generique = reinterpret_cast<struct sockaddr*>(&mon_adresse);

        if (connect(sock.get(), ptr_generique, sizeof(mon_adresse)) == 0) {
            std::cout << "   -> Connexion reussie avec le style C++98 !\n";
        } else {
            std::cout << "   -> Echec de la connexion.\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception : " << e.what() << '\n';
    }

    return 0; // Le destructeur de Socket et AddrInfoGuard font le ménage ici
}
