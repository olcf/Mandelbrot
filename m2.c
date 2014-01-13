#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <omp.h>
#include <setjmp.h>
#include <tiffio.h>
#include <stdbool.h>
#include <float.h>
#include "mpi.h"

#define nx 2000
#define ny 2000
#define THREAD_NUM_PER_NODE 8

int MSet[nx*ny];
int maxiter= 2000;			//max number of iterations
int myRank;

void calc_pixel_value(int calcny, int calcnx, int calcMSet[calcnx*calcny], int calcmaxiter);
void calcSet(int max_16, int max_16_last, int chunkSize, int *localMSet);

int main(int argc, char *argv[])
{
	MPI_Init(&argc, &argv);
	int *localMSet = NULL;
	int commSize;
	MPI_Comm_size(MPI_COMM_WORLD, &commSize);
	MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
	//Check if resolution is evenly divisible by num of nodes so all chunks are same size
	if ((nx*ny)%(commSize) != 0)
	{
		printf("Incompatible number of nodes\n");
		exit(0);
	}
	
	//Calculate size of chunks
	int chunkSize = 0;
	chunkSize = (nx*ny)/(commSize);
	localMSet = (int*)malloc((chunkSize-1) * (nx-1) * sizeof(int));
	memset(localMSet,0,chunkSize*sizeof(int));
	memset(MSet,0,nx*ny*sizeof(int));
	//Scatter chunk of array to each node

	int max_16 = (chunkSize/THREAD_NUM_PER_NODE);
	int max_16_last = 0;
	//break into 16 parts 1/core
	if ((chunkSize%THREAD_NUM_PER_NODE) != 0)
	{
		int y = 0;
		y = (max_16*(THREAD_NUM_PER_NODE-1));
		max_16_last = (chunkSize - y);	
	}
	else
	{
		max_16_last = max_16;
	}
	
	//Start OpenMP code
	calcSet(max_16, max_16_last, chunkSize, localMSet);
	
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Gather(localMSet, chunkSize, MPI_INT, MSet, chunkSize, MPI_INT, 0, MPI_COMM_WORLD);
	if (myRank ==0)
	{
		calc_pixel_value(nx,ny,MSet,maxiter);
	}
	MPI_Finalize();
}

void calcSet(int max_16, int max_16_last, int chunkSize, int* localMSet)
{
	int xmin=-3, xmax= 1; 		//low and high x-value of image window
	int ymin=-2, ymax= 2;			//low and high y-value of image window
	double threshold = 1.0;
	double dist = 0.0;
	int ix, iy;
	double cx, cy;
	int iter, i = 0;
	double x,y,x2,y2 = 0.0;
	double temp=0.0;
	double xder=0.0;
	double yder=0.0;
	double xorbit[maxiter+1];
	xorbit[0] = 0.0;
	double yorbit[maxiter+1];
	yorbit[0] = 0.0;
	double huge = 100000;
	bool flag = false;
	const double overflow = DBL_MAX;
	int ompmax = 0;
	int size = 0;
	double delta = (threshold*(xmax-xmin))/(double)(nx-1);

	#pragma omp parallel shared(localMSet) firstprivate(size,iter,ompmax,cx,cy,ix,iy,i,x,y,x2,y2,temp,xder,yder,dist,yorbit,xorbit,flag) num_threads(THREAD_NUM_PER_NODE)
	{
		if (omp_get_thread_num() == THREAD_NUM_PER_NODE-1)
		{
			ompmax = (chunkSize-1);
			size = (omp_get_thread_num()*max_16_last);
		}
		else
		{
			ompmax = (omp_get_thread_num()+1)*max_16 - 1;
			size = omp_get_thread_num()*max_16;
		}
	
		for (iy=size; iy<=ompmax; iy++)
		{	
			cy = ymin+iy*(ymax-ymin)/(double)(ny-1);
			for (ix = 0; ix<=(nx-1); ix++)
			{
				iter = 0;
				i = 0;
				x = 0.0;
				y = 0.0;
				x2 = 0.0;
				y2 = 0.0;
				temp = 0.0;
				xder = 0.0;
				yder = 0.0;
				dist = 0.0;
				cx = xmin +ix*(xmax-xmin)/(double)(ny-1);
	
				for (iter =0; iter<=maxiter; iter++)
				{
					//Begin normal mandel level set process
					temp = x2-y2 +cx;
					y = 2.0*x*y+cy;
					x = temp;
					x2 = x*x;
					y2 = y*y;
					xorbit[iter+1]=x;
					yorbit[iter+1]=y;
					if (x2+y2>huge) break;	//if point escapes then break to next loop
				}
				//if the point escapes, find the distance from the set
				if (x2+y2>=huge)
				{
					xder, yder = 0;
					i = 0;
					flag = false;
	
					for (i=0;i<=iter && flag==false;i++)
					{
						temp = 2.0*(xorbit[i]*xder-yorbit[i]*yder)+1;
						yder = 2.0*(yorbit[i]*xder+xorbit[i]*yder);
						xder = temp;
						flag = fmax(fabs(xder), fabs(yder)) > overflow;
					}
					if (flag == false)
					{
						dist=(log(x2+y2)*sqrt(x2+y2))/sqrt(xder*xder+yder*yder); 
						//printf("DIST:%d\n", dist);
					}	
	
				}
				
				if (nx*ix +iy > 799999)
					//printf("Rank: %d\tiy: %d\tix: %d\n", myRank, iy, ix);
				if (dist < delta)
					localMSet[(nx-1) * ix + iy] = 1;
				else
					localMSet[(nx-1) * ix + iy] = 0;
				if (localMSet[((nx-1)*ix)+iy] == 1)
				{
					//printf("Rank: %d\tlocalMSet[%d]\tValue: %d\n", myRank, (((nx-1)*ix)+iy), localMSet[((nx-1)*ix) + iy]);
				}
				//printf("MSET:%d\n",MSet[ix][iy]);
			}
		}
	}
}