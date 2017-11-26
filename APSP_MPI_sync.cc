#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <iterator>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <mpi.h>

#define MAX(x, y) ((x)>(y)?(x):(y))
#define MIN(x, y) ((x)<(y)?(x):(y))


#define INF 999999999

//#define _DEBUG_
#define _MEASURE_TIME

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
    double total_waittime=0;
    double total_block1=0;
    double total_block2=0;
    double total_block3=0;
    double total_block4=0;
    double total_block5=0;
    double exe_st=0;
    double exe_ed=0;
    #define ST exe_st
    #define ED exe_ed
    #define EXE total_exetime
    #define CALC total_calctime
    #define IO total_calctime
    #define COMM total_commtime
    #define WAIT total_waittime
    #define B1 total_block1
    #define B2 total_block2
    #define B3 total_block3
    #define B4 total_block4
    #define B5 total_block5

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
    #define WAIT
    #define B1
    #define B2
    #define B3
    #define B4
    #define B5
#endif


#ifdef _DEBUG_
    int __print_step = 0;

    void __pt_log(const char *f_, ...){
        std::stringstream ss;
        ss << "[Rank %d] Step %08d: " << f_ <<'\n';
        std::string format = ss.str();

        va_list va;
        va_start(va, f_);
            vprintf(format.c_str(), va);
        va_end(va);
        __print_step++;
    }

    #define VA_ARGS(...) , ##__VA_ARGS__
    #define LOG(f_, ...) __pt_log((f_), world_rank, __print_step VA_ARGS(__VA_ARGS__))
#else
    #define LOG(F_, ...)
#endif


int world_size;
int world_rank;
int graph_rank;

int vert;
int edge;

MPI_Comm COMM_GRAPH;


int *data;
int *buf;
std::vector<int> neig;
int neig_count;


inline void init(){
    data = new int[vert];
    buf = new int[vert];
    std::fill(data, data+vert, INF);
    data[world_rank] = 0;
    neig.reserve(vert);
}

inline void finalize(){
    delete [] data;
    delete [] buf;    
}

inline void calc(){
    for(int i=0;i<vert;++i){
        if( i != world_rank && data[i] != INF){
            neig.push_back(i);
        }
    }

    neig_count = neig.size();
}

inline int update(int id){
    int up = 0;
    for(int i=0;i<vert;++i){
        if(data[i] > data[id] + buf[i]){
            data[i] = data[id] + buf[i];
            up = 1;
        }
    }
    return up;
}

inline void dump_from_file(const char *file){

    std::ifstream fin(file);
    std::stringstream ss;

    TIC;{
    ss << fin.rdbuf();

    }TOC_P(IO);

    ss >> vert >> edge;

    init();

    int i, j, w;
    for(int e=0;e<edge;++e){
        ss >> i >> j >> w;
        if(i==world_rank)data[j]=w;
        else if(j==world_rank)data[i]=w;
    }
}

inline void create_graph(){
    MPI_Dist_graph_create(MPI_COMM_WORLD, 1, &world_rank, &neig_count,
            neig.data(), MPI_UNWEIGHTED, MPI_INFO_NULL, false, &COMM_GRAPH);
    MPI_Comm_rank(COMM_GRAPH, &graph_rank);
    //COMM_GRAPH = MPI_COMM_WORLD;
    //graph_rank = world_rank;
}

inline void floyd(){

    MPI_Request *send_req = new MPI_Request[vert];

    int not_done = 1;
    while(not_done){
        not_done = 0;
        for(int i=0;i<neig_count;++i){
            MPI_Isend(data, vert, MPI_INT, neig[i], 1, COMM_GRAPH, send_req+i);
            LOG("send to %d", neig[i]);
        }
        for(int i=0;i<neig_count;++i){
            TIC;{

            MPI_Recv(buf, vert, MPI_INT, neig[i], 1, COMM_GRAPH, MPI_STATUS_IGNORE);
 
            }TOC_P(COMM);
            TIC;{

            not_done |= update( neig[i] );

            }TOC_P(CALC);
        }
        TIC;{
        MPI_Allreduce(MPI_IN_PLACE, &not_done, 1, MPI_INT, MPI_LOR, MPI_COMM_WORLD);
        }TOC_P(WAIT);
    }

    delete [] send_req;
}

inline void dump_to_file(const char *file){
    std::stringstream ss;
    
    TIC;
    std::ostream_iterator<int> out(ss, " ");
    std::copy(data, data+vert, out);
    ss << '\n';

    std::string str = ss.str();
    int *len = new int[vert]();
    len[world_rank] = str.size();
    
    MPI_File fout;
    MPI_File_delete(file, MPI_INFO_NULL);
    MPI_File_open(MPI_COMM_WORLD, file, MPI_MODE_WRONLY | MPI_MODE_CREATE, MPI_INFO_NULL, &fout);

    TOC_P(IO);

    TIC;{
    MPI_Allreduce(MPI_IN_PLACE, len, vert, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    }TOC_P(COMM);

    int offset=0;
    for(int i=0;i<world_rank;++i){
        offset += len[i];
    }

    TIC;{

    MPI_File_set_view(fout, offset, MPI_CHAR, MPI_CHAR, "native", MPI_INFO_NULL);
    MPI_File_write_all(fout, str.c_str(), len[world_rank], MPI_CHAR, MPI_STATUS_IGNORE);

    MPI_File_close(&fout);
    
    }TOC_P(IO);

    delete [] len;
}

int main(int argc, char **argv){

    assert(argc == 4);

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    
    TIME(ST);
    TIC;
    dump_from_file(argv[1]);
    TOC_P(B1);

    LOG("file read");

    TIC;
    calc();
    TOC_P(B2);

    TIC;
    create_graph();
    TOC_P(B3);

    TIC;
    floyd();
    TOC_P(B4);    

    TIC;
    dump_to_file(argv[2]);
    TOC_P(B5);

#ifdef _MEASURE_TIME
    TIME(ED);
    EXE = ED - ST;
    //rank, EXE, CALC, WAIT, IO, COMM, PROC
    printf("%d, %lf, %lf, %lf, %lf, %lf, %lf, %lf, %lf, %lf, %lf, %lf\n", world_rank, EXE, CALC, WAIT, IO, COMM, EXE-CALC-WAIT-IO-COMM, B1, B2, B3, B4, B5);
#endif

    finalize();
    MPI_Finalize();
    return 0;
}
