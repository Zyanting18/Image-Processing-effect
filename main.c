#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <openacc.h>
#include <ctype.h>


//================================================
// ppmFile.h
//================================================

typedef struct Image
{
  int width;
  int height;
  unsigned char *data;
} Image;

Image *ImageCreate(int width, int height);
Image *ImageRead(char *filename);
void   ImageWrite(Image *image, char *filename);
int    ImageWidth(Image *image);
int    ImageHeight(Image *image);
void   ImageClear(Image *image, unsigned char red, unsigned char green, unsigned char blue);
void   ImageSetPixel(Image *image, int x, int y, int chan, unsigned char val);
unsigned char ImageGetPixel(Image *image, int x, int y, int chan);


//================================================
// The Blur Filter
//================================================
void ProcessImageACC(Image **data, int filterRad, Image **output){
    
  int width = (*data)->width;
  int height = (*data)->height;
  unsigned char *input = (*data)->data;
  unsigned char *result = (*output)->data;

  //process the image
  #pragma acc data copyin(input[0:width*height*3]) copyout(result[0:width*height*3])
  #pragma acc kernels present_or_copyin(input[0:width*height*3])
  #pragma acc loop independent
  for(int j = 0; j < height; j++){
      #pragma acc loop independent
      for(int i = 0; i < width; i++){
 
      /*
       * 0 0 0
       * 0 1 0
       * 0 0 0
       */
   	  double redTotal = 0;
  	  double greenTotal = 0;
  	  double blueTotal = 0;
  	  
      int count = 0;
      //for each pixel calculate the new colour value
      #pragma acc loop independent
      for ( int row = -filterRad; row <=filterRad ; row++){
        #pragma acc loop independent
        for (int col = -filterRad; col<=filterRad; col++){
          int currentX = i + col;
          int currentY = j + row;
          if (currentX >= 0 && currentX < width && currentY >= 0 && currentY < height){
            unsigned char red = input[(currentX + currentY * width )*3];
            unsigned char green = input[(currentX + currentY * width )*3+1];
            unsigned char blue = input[(currentX + currentY * width )*3 +2];

            redTotal += red;
            greenTotal += green;
            blueTotal += blue;

            count++;
            
          }
        }
      }
      

      unsigned char red = redTotal / count;
      unsigned char green = greenTotal / count;
      unsigned char blue = blueTotal / count;
      
      result[(i + j * width)*3] = red;
      result[(i + j * width)*3+1] = green;
      result[(i + j * width)*3+2] = blue;


      }

  }

}


//================================================
// Main Program
//================================================
int main(int argc, char* argv[]){

  if (argc !=4 ){
    printf("Usage: %s radius <inputFilename>.ppm <outputFilename>.ppm\n", argv[0]);
      return 1;
  }
  
  //vars used for processing:
  Image *data, *result;
  int dataSize;
  int filterRadius = atoi(argv[1]); //radius

  //===read the data===
  data = ImageRead(argv[2]); //read inputfilename into data

  //===send data to nodes===
  //send data size in bytes
  dataSize = sizeof(unsigned char)*data->width*data->height*3; // data.width * data.height *3 

  //===process the image===
  //allocate space to store result
  result = (Image*)malloc(sizeof(Image));
  result->data = (unsigned char*)malloc(dataSize);
  result->width = data->width;  
  result->height = data->height;

  //initialize all to 0
  for(int i = 0; i < (result->width*result->height*3); i++){
      result->data[i] = 0;
  }

  struct timeval start, end, duration; 
  gettimeofday(&start, NULL);
  //apply teh filter
  ProcessImageACC(&data, filterRadius, &result);
  
  gettimeofday(&end,NULL);
  timersub(&end, &start, &duration); 
  printf("%f \n", duration.tv_sec+duration.tv_usec/1000000.0);

  //===save the data back===
  ImageWrite(result, argv[3]);

  return 0;
}


//================================================
// ppmFile.c
//================================================

/************************ private functions ****************************/
static void die(char *message){
  fprintf(stderr, "ppm: %s\n", message);
  exit(1);
}
/* check a dimension (width or height) from the image file for reasonability */
static void checkDimension(int dim){
  if (dim < 1 || dim > 4000) 
    die("file contained unreasonable width or height");
}
/* read a header: verify format and get width and height */
static void readPPMHeader(FILE *fp, int *width, int *height){
  char ch;
  int  maxval;
  if (fscanf(fp, "P%c\n", &ch) != 1 || ch != '6') 
    die("file is not in ppm raw format; cannot read");
  /* skip comments */
  ch = getc(fp);
  while (ch == '#')
    {
      do {
  ch = getc(fp);
      } while (ch != '\n'); /* read to the end of the line */
      ch = getc(fp);            /* thanks, Elliot */
    }
  if (!isdigit(ch)) die("cannot read header information from ppm file");
  ungetc(ch, fp);   /* put that digit back */
  /* read the width, height, and maximum value for a pixel */
  fscanf(fp, "%d%d%d\n", width, height, &maxval);
  if (maxval != 255) die("image is not true-color (24 bit); read failed");
  checkDimension(*width);
  checkDimension(*height);
}
Image *ImageCreate(int width, int height){
  Image *image = (Image *) malloc(sizeof(Image));
  if (!image) die("cannot allocate memory for new image");
  image->width  = width;
  image->height = height;
  image->data   = (unsigned char *) malloc(width * height * 3);
  if (!image->data) die("cannot allocate memory for new image");
  return image;
}
Image *ImageRead(char *filename){
  int width, height, num, size;
  unsigned  *p;
  Image *image = (Image *) malloc(sizeof(Image));
  FILE  *fp    = fopen(filename, "r");
  if (!image) die("cannot allocate memory for new image");
  if (!fp)    die("cannot open file for reading");
  readPPMHeader(fp, &width, &height);
  size          = width * height * 3;
  image->data   = (unsigned  char*) malloc(size);
  image->width  = width;
  image->height = height;
  if (!image->data) die("cannot allocate memory for new image");
  num = fread((void *) image->data, 1, (size_t) size, fp);
  if (num != size) die("cannot read image data from file");
  fclose(fp);
  return image;
}
void ImageWrite(Image *image, char *filename){
  int num;
  int size = image->width * image->height * 3;
  FILE *fp = fopen(filename, "w");
  if (!fp) die("cannot open file for writing");
  fprintf(fp, "P6\n%d %d\n%d\n", image->width, image->height, 255);
  num = fwrite((void *) image->data, 1, (size_t) size, fp);
  if (num != size) die("cannot write image data to file");
  fclose(fp);
} 
int ImageWidth(Image *image){
  return image->width;
}
int ImageHeight(Image *image){
  return image->height;
}
void ImageClear(Image *image, unsigned char red, unsigned char green, unsigned char blue){
  int i;
  int pix = image->width * image->height;
  unsigned char *data = image->data;
  for (i = 0; i < pix; i++)
    {
      *data++ = red;
      *data++ = green;
      *data++ = blue;
    }
}
void ImageSetPixel(Image *image, int x, int y, int chan, unsigned char val){
  int offset = (y * image->width + x) * 3 + chan;
  image->data[offset] = val;
}
unsigned  char ImageGetPixel(Image *image, int x, int y, int chan){
  int offset = (y * image->width + x) * 3 + chan;
  return image->data[offset];
}
