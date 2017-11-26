//cpp header
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <string>
#include <streambuf>

//c header
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <time.h>

//const
#define UNK 999999999
#define MAX_VERTEX 2000
#define PTHREAD_LIMIT 2000
#define B 1000000000.0

//func
#define MIN(x, y) ((x)<(y)?(x):(y))
#define MAX(x, y) ((x)>(y)?(x):(y))


#define _MEASURE_TIME

#ifdef _MEASURE_TIME
    struct timespec __temp_time;
    struct timespec __temp_time2;
    #define __CTIME(X) clock_gettime(CLOCK_MONOTONIC, &X)
    #define __CALC_TIME(X, Y) ((Y.tv_sec -X.tv_sec)+(Y.tv_nsec-X.tv_nsec)/B)
    #define TIC     __CTIME(__temp_time)
    #define TOC(X)  __CTIME(__temp_time2); X = ((__temp_time2.tv_sec -__temp_time.tv_sec)+(__temp_time2.tv_nsec-__temp_time.tv_nsec)/B)
    #define TOC_P(X) __CTIME(__temp_time2); X += ((__temp_time2.tv_sec -__temp_time.tv_sec)+(__temp_time2.tv_nsec-__temp_time.tv_nsec)/B)
    #define TIME(X) __CTIME(__temp_time2); X = (__temp_time2.tv_sec+__temp_time2.tv_nsec/B)

    struct timespec *__temp_times;
    struct timespec *__temp_times2;
    double *total_calctime;
    double *total_waittime;
    double total_iotime=0;
    double total_commtime=0;
    double total_exetime=0;
    double exe_st=0;
    double exe_ed=0;
    #define ST exe_st
    #define ED exe_ed
    #define CALC total_calctime[id]
    #define WAIT total_waittime[id]
    #define IO total_iotime
    #define COMM total_commtime
    #define EXE total_exetime
#else
    #define __CTIME(X)
    #define __CALC_TIME(X, Y)
    #define TIC
    #define TOC(X)
    #define TOC_P(X)
    #define TIME(X)

    #define ST
    #define ED
    #define CALC
    #define WAIT
    #define IO
    #define COMM
    #define EXE
#endif


#define parallel_output


int vert;
int edge;


int num_threads;
pthread_t *threads;
int *ID;

int valid_size;


pthread_barrier_t barr;

///////////////////////////////////////////////
struct Map{
    int *data;
    int **ptr;
    std::stringstream *oss;
    
    Map(){}
    ~Map(){ 
        delete [] data;
        delete [] ptr;
        delete [] oss;
    }

    inline void init(const int &v){
        data = new int[v*v];
        ptr = new int*[v];
        oss = new std::stringstream[v];
        std::fill(data, data+v*v, UNK);

        for(int i=0;i<vert;++i){
            ptr[i] = &data[i*v];
            ptr[i][i] = 0;
        }

    }

    inline int* operator[](const size_t &index){
        return ptr[index];
    }
};
///////////////////////////////////////////////

Map map;


inline void dump_from_file(char *file){

    std::ifstream fin(file);
    std::stringstream ss;

    TIC;
    ss << fin.rdbuf();
    TOC_P(IO);

    ss >> vert >> edge;

    map.init(vert);

    for (int e=0;e<edge;++e){
        int i,j,w;
        ss >> i >> j >> w;
        map[i][j] = map[j][i] = w;
    }
}



inline void dump_to_file(char *file){

    std::stringstream ss;

    int *iter=map.data;
    for(int i=0;i<vert;++i){
        for(int j=0;j<vert;++j){
            ss << *iter << ' ';
            ++iter;
        }
        ss << '\n';
    }
    

    std::ofstream fout(file);

    fout << ss.rdbuf();
    fout.close();
}

inline void parallel_dump_to_file(const int &id){
    for(int i=id;i<vert;i+=valid_size){
        for(int j=0;j<vert;++j){
            map.oss[i] << map[i][j] << ' ';
        }
        map.oss[i] << '\n';
    }
}


void *task(void* var){
    
    int id = * ((int*)var);
    /*
    int dv = vert / valid_size;
    int rm = vert % valid_size;
    int bn = (id < rm) ? 1:0;

    int sz = bn + dv;
    int st = dv * id + (bn ? id:rm);
    int ed = st + sz;
    */


    for(int k=0;k<vert;++k){

#ifdef _MEASURE_TIME
        __CTIME(__temp_times[id]);
#endif

        for(int i=id;i<vert;i+=valid_size){
            for(int j=0;j<vert;++j){
                map[i][j] = MIN(map[i][k]+map[k][j], map[i][j]);
            }
        }

#ifdef _MEASURE_TIME
        __CTIME(__temp_times2[id]);
        total_calctime[id] += __CALC_TIME(__temp_times[id], __temp_times2[id]);
#endif

        pthread_barrier_wait(&barr);

#ifdef _MEASURE_TIME
        __CTIME(__temp_times[id]);
        total_waittime[id] += __CALC_TIME(__temp_times2[id], __temp_times[id]);
#endif

    }
    
#ifdef parallel_output
    parallel_dump_to_file(id);
#endif

    pthread_exit(NULL);
}

int main(int argc, char **argv){

    // check for argument count
    assert(argc == 4);

    TIME(ST);

    num_threads = atoi(argv[3]);

    dump_from_file(argv[1]);

    valid_size = (vert < num_threads) ? vert:num_threads;

    threads = new pthread_t[valid_size];
    ID = new int[valid_size];

#ifdef _MEASURE_TIME
    total_calctime = new double[valid_size]{};
    total_waittime = new double[valid_size]{};
    __temp_times = new struct timespec[valid_size]{};
    __temp_times2 = new struct timespec[valid_size]{};
#endif

    pthread_barrier_init(&barr, NULL, valid_size);

    // parallel region
    {
        for(int i=0;i<valid_size;++i){
            ID[i] = i;
            pthread_create(&threads[i], NULL, task, (void*)&ID[i]);
        }

        for(int i=0;i<valid_size;++i){
            pthread_join(threads[i], NULL);
        }

    }

    pthread_barrier_destroy(&barr);

#ifdef parallel_output
    std::ofstream fout(argv[2]);
    for(int i=1;i<vert;++i){
        map.oss[0] << map.oss[i].rdbuf();
    }

    TIC;
    fout << map.oss[0].rdbuf();
    TOC_P(IO);
    fout.close();
#else
    dump_to_file(argv[2]);
#endif

    TIME(ED);

    delete [] threads;
    delete [] ID;

#ifdef _MEASURE_TIME   
    EXE = ED - ST;
    //ID, EXE, calc, io, others

    double avg_calctime = 0;
    double avg_waittime = 0;
    
    for(int i=0;i<valid_size;++i){
        avg_calctime += total_calctime[i];
        avg_waittime += total_waittime[i];     
    }

    avg_calctime /= valid_size;
    avg_waittime /= valid_size;
    
    //EXE, calc, wait, io, others
    printf("%lf, %lf, %lf, %lf, \n", EXE, avg_calctime, avg_waittime, IO, (EXE-avg_calctime-avg_waittime-IO));


    delete [] total_calctime;
    delete [] total_waittime;
    delete [] __temp_times;
    delete [] __temp_times2;
#endif

    return 0;
}
