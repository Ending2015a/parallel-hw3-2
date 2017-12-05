#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include <vector>
#include <queue>
#include <list>
#include <future>
#include <functional>
#include <iterator>
#include <algorithm>
#include <thread>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <pthread.h>
#include <mpi.h>
#include <sched.h>
#include <unistd.h>

#define MAX(x, y) ((x)>(y) ? (x):(y))
#define MIN(x, y) ((x)<(y) ? (x):(y))

#define INF 99999999
#define MAX_THREAD 11

//#define _DEBUG_

#ifdef _MEASURE_TIME
    double __temp_time=0;
    #define TIC     __temp_time = MPI_Wtime()
    #define TOC(X)  X = (MPI_Wtime() - __temp_time)
    #define TOC_P(X) X += (MPI_Wtime() - __temp_time)
    #define TIME(X) X = MPI_Wtime()

    double total_exetime=0;
    double total_calctime=0;
    double total_iotime=0;
    double total_commtime=0;
    double exe_st=0;
    double exe_ed=0;
    #define ST exe_st
    #define ED exe_ed
    #define EXE total_exetime
    #define CALC total_calctime
    #define IO total_calctime
    #define COMM total_commtime

#else
    #define TIC
    #define TOC(X)
    #define TOC_P(X)
    #define TIME(X)

    #define ST
    #define ED
    #define EXE
    #define CALC
    #define IO
    #define COMM
#endif

#ifdef _DEBUG_
    pthread_mutex_t __debug_log_mutex = PTHREAD_MUTEX_INITIALIZER;
    int __print_step = 0;

    void __pt_log(const char *f_, ...){
        std::stringstream ss;
        ss << "[Rank %d] Time %.8lf: " << f_ <<'\n';
        std::string format = ss.str();

        va_list va;
        va_start(va, f_);
            vprintf(format.c_str(), va);
        va_end(va);
        __print_step++;
    }

    #define VA_ARGS(...) , ##__VA_ARGS__
    #define LOG(f_, ...) pthread_mutex_lock(&__debug_log_mutex); \
                         __pt_log((f_), world_rank, MPI_Wtime() VA_ARGS(__VA_ARGS__)); \
                         pthread_mutex_unlock(&__debug_log_mutex)
#else
    #define LOG(f_, ...)
#endif


//*****************************************************

struct Map;
struct Client;

int num_thread;
int world_size;
int world_rank;

int vert, edge;

std::vector<std::vector<int>> global_subscribe_list;

//******************* DATA STRUCTURE *******************

// table
struct Map{
public:
    Map(){}
    ~Map(){ delete[] data; delete[] table; }

    // initialize table
    inline void init(int vert){
        data = new int[vert*vert];
        table = new int*[vert];
        
        std::fill(data, data + vert*vert, INF);
        for(int i=0;i<vert;++i){
            table[i] = data + i*vert;
            table[i][i] = 0;
        }
    }
    
    //operator [] overload
    inline int *operator[](const int &index){
        return table[index];
    }
public:
    //original data
    int *data;
    int **table;
};

//pack all nodes which thread need to serve as a client struct
struct Client{
public:

    Client() : sub_lock(PTHREAD_MUTEX_INITIALIZER), 
            pub_lock(PTHREAD_MUTEX_INITIALIZER),
            flag_lock(PTHREAD_MUTEX_INITIALIZER),
            update_flag(true),
            updating(false){}
    ~Client(){}

    //subscriber updated queue
    std::list<int> sub_queue;
    //publisher updated queue
    std::list<int> pub_queue;

    //store if client has update
    int has_update;

    pthread_mutex_t sub_lock;
    pthread_mutex_t pub_lock;
    pthread_mutex_t flag_lock;

    //node pack
    struct Node{
    public:
        Node(int ID){ this->ID=ID; }
        int ID;
        std::vector<int> neighbor_list;
        int neighbor_count;
    };

    bool update_flag;
    bool updating;
    std::vector<std::vector<int>> subscribe_list;
    std::vector<Node> node_list;
    int node_count;
};
//**********************************************

Map map;

std::vector<Client> clients;

//modularized thread (defined in threading.hpp)
// can use attach() func to attach any job to the thread
//ed::ModularizedThread *services;

int *Thread_ID;
pthread_t *services;

pthread_mutex_t global_mutex;
pthread_cond_t global_cond;
int global_counter{0};

bool not_termination{true};

//********* PTHREAD PARALLEL REGION ***********

inline void create_neighbor_list(Client* client){
    for(int i=0;i<client->node_count;++i){
        // vertex ID
        int ID = client->node_list[i].ID;
        // pointer to the neighbor list of vertex $ID
        auto &list = client->node_list[i].neighbor_list;
        list.reserve(vert);
        // creating neighbor list
        for(int j=0;j<vert;++j){
            if(j != ID && map[ID][j] != INF)
                list.push_back(j);
        }

        //calculate neighbors count
        client->node_list[i].neighbor_count = list.size();
        client->pub_queue.push_back(ID);
    }
}

inline void create_subscribe_list(Client* client){
    client->subscribe_list = std::move(std::vector<std::vector<int>>(vert));
    for(int i=0;i<client->node_count;++i){
        for(int j=0;j<client->node_list[i].neighbor_count;++j){
            client->subscribe_list[client->node_list[i].neighbor_list[j]].push_back(i);  //node_list index
        }
    }
}

inline void wait_for_global_subscribe_list(){
    pthread_mutex_lock(&global_mutex);
    ++global_counter;
    pthread_cond_wait(&global_cond, &global_mutex);
    pthread_mutex_unlock(&global_mutex);
}

inline int update_list(int node , int id){
    if(node == id)return 0;
    bool up = 0;
    for(int i=0;i<vert;++i){
        if(map[node][i] > map[node][id] + map[id][i]){
            map[node][i] = map[node][id] + map[id][i];
            up = 1;
        }
    }
    return up;
}

void pthread_task(int ID, Client *client){
    LOG("[thread %d] create neighbor list", ID);
    create_neighbor_list(client);
    LOG("[thread %d] create subscribe list", ID);
    create_subscribe_list(client);
    LOG("[thread %d] wait for global subscribe list", ID);
    wait_for_global_subscribe_list();

    std::vector<int> update_state(client->node_count, 0);
    while(not_termination){
        //subscribe
        pthread_mutex_lock(&client->sub_lock);
        if(client->sub_queue.empty()){
            pthread_mutex_unlock(&client->sub_lock);
            continue;
        }
        std::list<int> temp_queue;
        temp_queue.swap(client->sub_queue);
        pthread_mutex_unlock(&client->sub_lock);
        client->updating = true;
        

        std::fill(update_state.begin(), update_state.end(), 0);
        for(auto iter=temp_queue.begin(); iter!=temp_queue.end(); ++iter){//neighbor (vertex ID)
            for(auto iter2=client->subscribe_list[*iter].begin(); iter2 != client->subscribe_list[*iter].end(); ++iter2){ //source (node_list index)
                int ID = client->node_list[*iter2].ID; //(node_list index => vertex ID)
                //source -> neighbor -> destination
                update_state[*iter2] |= update_list(ID, *iter);
                //LOG("[thread %d] node %d update with node %d", thread_ID, ID, *iter);
            }
        }
        
        //publish
        pthread_mutex_lock(&client->pub_lock);
        for(int i=0;i<client->node_count;++i){
            if(update_state[i]){
                client->pub_queue.push_back(client->node_list[i].ID);
                client->update_flag = true;
            }
        }
        pthread_mutex_unlock(&client->pub_lock);
        client->updating = false;
    }

    //me->Terminate();

    LOG("[thread %d] end task", ID);
}

void *pure_pthread_task(void *args){
    int ID = *(int*)args;
    pthread_task(ID, &clients[ID]);
    return NULL;
}

//**********************************************

inline void create_global_subscribe_list(){
    global_subscribe_list = std::move(std::vector<std::vector<int>>(vert));

    while(1){
        pthread_mutex_lock(&global_mutex);
        if(global_counter >= num_thread){
            pthread_mutex_unlock(&global_mutex);
            break;
        }
        pthread_mutex_unlock(&global_mutex);
    }
    LOG("[main thread] global counter has reached the number %d", global_counter);

    for(int i=0;i<vert;++i){
        for(int j=0;j<clients.size();++j){
            if(clients[j].subscribe_list[i].size() > 0){
                global_subscribe_list[i].push_back(j);
            }
        }
    }

    //awake all pthreads to work
    pthread_cond_broadcast(&global_cond);
}

inline bool check_termination(){
    //std::stringstream ss;
    bool done = true;
    for(auto iter = clients.begin(); iter != clients.end(); ++iter){
        if(iter->update_flag) {
            iter->update_flag = false;
            done = false;
        }
    }
    return done;
}

inline bool not_updating(){
    for(auto iter = clients.begin(); iter != clients.end(); ++iter){
        if(iter->updating)
            return false;
    }
    return true;
}

//task for main thread (handle MPI Communication)
inline void communication_task(){
    LOG("[main thread] create global subscribe list");
    create_global_subscribe_list();

    MPI_Request send_request;
    MPI_Request recv_request;
    MPI_Status send_status;
    MPI_Status recv_status;

    bool done;
    bool get_handle = false;
    bool broadcast_once = false;

    int handle{1};

    if(world_rank == 0){
        MPI_Isend(&handle, 1, MPI_INT, world_rank+1, vert+1, MPI_COMM_WORLD, &send_request);
        MPI_Request_free(&send_request);
    }

    while(not_termination){

        //publish
        for(int i=0;i<clients.size();++i){
            pthread_mutex_lock(&clients[i].pub_lock);
            if(clients[i].pub_queue.empty() && handle != 3){
                pthread_mutex_unlock(&clients[i].pub_lock);
                continue;
            }
            std::list<int> temp_queue;
            /*
            if(handle == 3 && broadcast_once){
                for(int j=0;j<clients[i].node_count;++j){
                    temp_queue.push_back(clients[i].node_list[j].ID);
                }
            }else*/
                temp_queue.swap(clients[i].pub_queue);
            pthread_mutex_unlock(&clients[i].pub_lock);

            //broadcast
            for(auto iter=temp_queue.begin(); iter!=temp_queue.end(); ++iter){
                //LOG("broadcast node %d", *iter);
                for(int j=0;j<world_size;++j){
                    if(j != world_rank){
                        MPI_Isend(map[*iter], vert, MPI_INT, j, *iter + 1, MPI_COMM_WORLD, &send_request);
                        MPI_Request_free(&send_request);
                    }
                }
                
                // to other pthread
                for(auto iter2=global_subscribe_list[*iter].begin(); iter2!=global_subscribe_list[*iter].end();++iter2){
                    pthread_mutex_lock(&clients[*iter2].sub_lock);
                    clients[*iter2].sub_queue.push_back(*iter);
                    pthread_mutex_unlock(&clients[*iter2].sub_lock);
                }
            }
        }

        if(handle == 3)
            broadcast_once = false;

        //termination condition
        if(get_handle && not_updating() ){
            LOG("get handle %d", handle);
            if(handle == 3){
                not_termination = false;
                if(world_rank != 0){
                    MPI_Isend(&handle, 1, MPI_INT, (world_rank+1) % world_size, vert+1, MPI_COMM_WORLD, &send_request);
                    get_handle = false;
                }
                break;
            }else{
                if(check_termination()){
                    if(world_rank == 0){
                        ++handle;
                        LOG("increase handle to %d", handle);
                    }
                }else{
                    handle = 0;
                    LOG("zero handle");
                }
                MPI_Isend(&handle, 1, MPI_INT, (world_rank+1) % world_size, vert+1, MPI_COMM_WORLD, &send_request);
                get_handle = false;
            }
        }

        //subscribe
        int flag=1;
        do{
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &recv_status);
            if(flag){
                int tag = recv_status.MPI_TAG - 1;
                if(tag < vert){
                    //LOG("recv node %d", tag);
                    MPI_Recv(map[tag], vert, MPI_INT, recv_status.MPI_SOURCE, recv_status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    for(auto iter=global_subscribe_list[tag].begin(); iter!=global_subscribe_list[tag].end();++iter){ //client index
                        pthread_mutex_lock(&clients[*iter].sub_lock);
                        clients[*iter].sub_queue.push_back(tag);
                        pthread_mutex_unlock(&clients[*iter].sub_lock);
                    }
                }else{
                    MPI_Recv(&handle, 1, MPI_INT, recv_status.MPI_SOURCE, recv_status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    get_handle = true;
                    if(handle == 3)
                        broadcast_once = true;
                }
            }
        }while(flag);

        
    }

    MPI_Wait(&send_request, MPI_STATUS_IGNORE);
}

//read file
inline void dump_from_file(const char *file){
    std::ifstream fin(file);
    std::stringstream ss;

    TIC;{
    ss << fin.rdbuf();

    }TOC_P(IO);

    ss >> vert >> edge;

    map.init(vert);

    int i, j, w;
    for(int e=0;e<edge;++e){
        ss >> i >> j >> w;
        map[i][j] = map[j][i] = w;
    }
}

inline void dump_to_file(const char *file){
    std::ofstream fout(file);
    std::stringstream ss;

    for(int i=0;i<vert;++i){
        for(int j=0;j<vert;++j){
            ss << map[i][j] << ' ';
        }
        ss << '\n';
    }

    fout << ss.rdbuf();
    fout.close();
}

//create thread
inline void create_service(){
    pid_t my_pid = getpid();
    cpu_set_t affinity;
    if(0 != sched_getaffinity(my_pid, sizeof(affinity), &affinity)){
        LOG("error during sched_getaffinity");
        return;
    }
    int cpus = 0;
    for(int i=0;i<CPU_SETSIZE;++i){
        if(CPU_ISSET(i, &affinity)){
            ++cpus;
        }
    }

    //LOG("pid %d is bind to %d cpus", my_pid, cpus);

    num_thread = MAX(cpus-1, 1);

    //create clients
    clients.resize(num_thread);
}

//distribude the nodes to each MPI node and thread(store in client struct)
inline void distribute_node_list(){

    int max_space = vert / world_size / num_thread;
    //create space
    //LOG("create space");
    for(auto iter = clients.begin(); iter != clients.end();++iter){
        iter->node_list.reserve(max_space);
    }

    //create node list
    //LOG("create node list");
    for(int i=world_rank ; i<vert ; i+=world_size){
        //construct node
        clients[i / world_size % num_thread].node_list.emplace_back(i);
    }

    //calculate node count
    //LOG("calculate node count");
    for(auto iter = clients.begin(); iter != clients.end(); ++iter){
        iter->node_count = iter->node_list.size();
    }

}

//attach task to each thread
inline void attach_services(){
    Thread_ID = new int[num_thread];
    services = new pthread_t[num_thread];
    for(int i=0;i<num_thread;++i){
        Thread_ID[i] = i;
        pthread_create(&services[i], NULL, pure_pthread_task, &Thread_ID[i]);
    }
}

int main(int argc, char **argv){

    assert(argc == 4);
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    pthread_mutex_init(&global_mutex, NULL);
    pthread_cond_init(&global_cond, NULL);

    //LOG("dump from file");
    dump_from_file(argv[1]);

    //LOG("create service");
    create_service();

    //LOG("distribute node list");
    distribute_node_list();

    //LOG("attach clients to services");
    attach_services();

    LOG("main thread is going to work");
    communication_task();


    for(int i=0;i<num_thread;++i){
        pthread_join(services[i], NULL);
    }
    delete[] services;
    delete[] Thread_ID;

    LOG("dump to file");
    if(world_rank == 0)
        dump_to_file(argv[2]);

    LOG("end task");

    MPI_Finalize();
    return 0;
}
