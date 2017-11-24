//    //<>// //<>// //<>// //<>//
//  LED_Mapper.pde
//  Lightwork-Mapper
//
//  Created by Leo Stefansson and Tim Rolls 
//
//  This sketch uses computer vision to automatically generate mapping for LEDs.
//  Currently, Fadecandy and PixelPusher are supported.

import processing.svg.*;
import processing.video.*; 
import gab.opencv.*;
import com.hamoid.*; // Video recording
import java.awt.Rectangle;

Capture cam;
Capture cam2;
Movie movie;
OpenCV opencv;
ControlP5 cp5;
Animator animator;
Interface network; 

boolean isMapping = false; 
int ledBrightness = 100;

enum  VideoMode {
  CAMERA, FILE, OFF
};

VideoMode videoMode; 
String movieFileName = "partialBinary.mp4";
boolean shouldSyncFrames; // Should we read one movie frame per program frame (slow, but maybe more accurate). 
color on = color(255, 255, 255);
color off = color(0, 0, 0);

int camWidth =640;
int camHeight =480;
float camAspect;
int camWindows = 2;
PGraphics camFBO;
PGraphics cvFBO;
PGraphics blobFBO;

int guiMultiply = 1;

int cvThreshold = 100;
float cvContrast = 1.15;

ArrayList <PVector>     coords;
String savePath;

ArrayList <LED>     leds;

int FPS = 30; 
VideoExport videoExport;
boolean isRecording = false;

PImage videoInput; 
PImage cvOutput;

ArrayList<Contour> contours;
// List of detected contours parsed as blobs (every frame)
ArrayList<Contour> newBlobs;
// List of my blob objects (persistent)
ArrayList<Blob> blobList;
// Number of blobs detected over all time. Used to set IDs.
int blobCount = 0; // Use this to assign new (unique) ID's to blobs
int minBlobSize = 5;
int maxBlobSize = 10;

// Window size
int windowSizeX, windowSizeY;

// Actual display size for camera
int camDisplayWidth, camDisplayHeight;
Rectangle camArea;

void setup()
{
  size(640, 480, P3D);
  frameRate(FPS);
  camAspect = (float)camWidth / (float)camHeight;
  println(camAspect);

  videoMode = VideoMode.CAMERA; 
  shouldSyncFrames = false; 
  println("creating FBOs");
  camFBO = createGraphics(camWidth, camHeight, P3D);
  cvFBO = createGraphics(camWidth, camHeight, P3D);
  blobFBO = createGraphics(camWidth, camHeight, P3D); 

  println("making arraylists for coords, leds, and bloblist");
  coords = new ArrayList<PVector>();
  leds =new ArrayList<LED>();

  // Blobs list
  blobList = new ArrayList<Blob>();

  cam = new Capture(this, camWidth, camHeight, 30);

  println("allocating video export");
  videoExport = new VideoExport(this, "data/"+movieFileName, cam);

  if (videoMode == VideoMode.FILE) {
    println("loading video file");
    movie = new Movie(this, movieFileName); // TODO: Make dynamic (use loadMovieFile method)
    // Pausing the video at the first frame. 
    movie.play();
    if (shouldSyncFrames) {
      movie.jump(0);
      movie.pause();
    }
  }

  // OpenCV Setup
  println("Setting up openCV");
  opencv = new OpenCV(this, camWidth, camHeight);
  opencv.startBackgroundSubtraction(2, 5, 0.5); //int history, int nMixtures, double backgroundRatio

  println("setting up network Interface");
  network = new Interface();
  network.setNumStrips(1);
  network.setNumLedsPerStrip(50); // TODO: Fix these setters...

  println("creating animator");
  animator =new Animator(); //ledsPerstrip, strips, brightness
  animator.setLedBrightness(ledBrightness);
  animator.setFrameSkip(3);
  animator.setAllLEDColours(off); // Clear the LED strips
  animator.setMode(animationMode.OFF);
  animator.update();

  //Check for hi resolution display
  println("setup gui multiply");
  guiMultiply = 1;
  if (displayWidth >= 2560) {
    guiMultiply = 2;
  }

  //set up window for 2d mapping
  window2d();

  println("calling buildUI on a thread");
  thread("buildUI"); // This takes more than 5 seconds and will break OpenGL if it's not on a separate thread

  // Make sure there's always something in videoInput
  println("allocating videoInput with empty image");
  videoInput = createImage(camWidth, camHeight, RGB);

  background(0);
}

void draw()
{
  //Loading screen
  if (!isUIReady) {
    cp5.setVisible(false);
    background(0);
    if (frameCount%1000==0) {
      println("DrawLoop: Building UI....");
    }

    int size = (millis()/5%255);

    pushMatrix(); 
    translate(width/2, height/2);
    //println((1.0/(float)size)%255);

    noFill();
    stroke(255, size);
    strokeWeight(4);
    //rotate(frameCount*0.1);
    ellipse(0, 0, size, size);

    translate(0, 100*guiMultiply);
    fill(255);
    noStroke();
    textSize(18*guiMultiply);
    textAlign(CENTER);
    text("LOADING...", 0, 0);

    popMatrix();


    return;
  } else if (!cp5.isVisible()) {
    cp5.setVisible(true);
  }

  if (videoMode == VideoMode.CAMERA && cam!=null ) { 
    cam.read();
    videoInput = cam;
  } else if (videoMode == VideoMode.FILE) {
    videoInput = movie;
    if (shouldSyncFrames) {
      nextMovieFrame();
    }
  } else {
    // println("Oops, no video input!");
  }

  //UI is drawn on canvas background, update to clear last frame's UI changes
  background(#222222);

  // Display the camera input and processed binary image
  camFBO.beginDraw();
  camFBO.image(videoInput, 0, 0, camWidth, camHeight);
  camFBO.endDraw();

  image(camFBO, 0, (70*guiMultiply), camDisplayWidth, camDisplayHeight);
  opencv.loadImage(camFBO);
  opencv.gray();
  //opencv.threshold(cvThreshold);

  //opencv.contrast(cvContrast);
  //opencv.dilate();
  //opencv.erode();
  //opencv.startBackgroundSubtraction(0, 5, 0.5); //int history, int nMixtures, double backgroundRatio
  //opencv.equalizeHistogram();
  //opencv.blur(2);
  opencv.updateBackground();

  cvFBO.beginDraw();
  cvFBO.image(opencv.getSnapshot(), 0, 0);

  if (coords.size()>0) {
    for (PVector p : coords) {
      cvFBO.noFill();
      cvFBO.stroke(255, 0, 0);
      cvFBO.ellipse(p.x, p.y, 10, 10);
    }
  }
  cvFBO.endDraw();
  image(cvFBO, camDisplayWidth, (70*guiMultiply), camDisplayWidth, camDisplayHeight);

  if (camWindows==3 && cam2!=null) {
    cam2.read();
    image(cam2, camDisplayWidth*2, (70*guiMultiply), camDisplayWidth, camDisplayHeight);
  }

  if (isMapping) {
    updateBlobs(); 
    sequentialMapping();

    //// Find and manage blobs
    //    decodeBlobs(); 
    //    // Decode the signal in the blobs

    //    //print(br);
    //    //print(", ");
    //    if (blobList.size() > 0) {
    //      blobList.get(0).decode(); // Decode the pattern
    //    }
  }


  //blobFBO.beginDraw();
  ////detectBlobs();
  ////displayBlobs();
  ////text("numBlobs: "+blobList.size(), 0, height-20); 
  ////displayContoursBoundingBoxes();
  //blobFBO.endDraw();

  animator.update();

  //show the array of colors going out to the LEDs
  if (showLEDColors) {
    // scale based on window size and leds in array
    float x = (float)width/ (float)leds.size(); //TODO: display is missing a bit on the right?
    for (int i = 0; i<leds.size(); i++) {
      fill(leds.get(i).c);
      noStroke();
      rect(i*x, (camArea.y+camArea.height)-(5*guiMultiply), x, 5*guiMultiply);
    }
  }

  if (isRecording) {
    videoExport.saveFrame();
  }
}

// Mapping methods
void sequentialMapping() {
  //for (Contour contour : opencv.findContours()) {
  //  noFill();
  //  stroke(255, 0, 0);
  //  //contour.draw();
  //  coords.add(new PVector((float)contour.getBoundingBox().getCenterX(), (float)contour.getBoundingBox().getCenterY()));
  //}

  if (blobList.size()!=0) {
    Blob current = blobList.get(blobList.size()-1);  //only keeping one
    println(blobList.size());
    Rectangle rect = blobList.get(blobList.size()-1).contour.getBoundingBox();
    PVector loc = new PVector(); 
    loc.set((float)rect.getCenterX(), (float)rect.getCenterY());

    //PVector loc = new PVector();
    //loc.set( (float)current.contour.getBoundingBox().getCenterX(), (float)current.contour.getBoundingBox().getCenterY());
    ////coords.add(new PVector((float)current.contour.getBoundingBox().getCenterX(), (float)current.contour.getBoundingBox().getCenterY()));

    //for (int i=0 ; i<leds.size() ; i++){ //<>//
    //}
    int index = animator.getLedIndex();
    //LED temp = 
    leds.get(index).setCoord(loc);
    //temp.setCoord(loc); 
    //leds.set(index,temp);
    coords.add(loc);
    println(loc);
  }

  displayBlobs();
}

void updateBlobs() {
  // Find all contours
  contours = opencv.findContours();

  // Filter contours, remove contours that are too big or too small
  // The filtered results are our 'Blobs' (Should be detected LEDs)
  newBlobs = filterContours(contours); // Stores all blobs found in this frame

  // Note: newBlobs is actually of the Contours datatype
  // Register all the new blobs if the blobList is empty
  if (blobList.isEmpty()) {
    //println("Blob List is Empty, adding " + newBlobs.size() + " new blobs.");
    for (int i = 0; i < newBlobs.size(); i++) {
      //println("+++ New blob detected with ID: " + blobCount);
      int id = blobCount; 
      blobList.add(new Blob(this, id, newBlobs.get(i)));
      blobCount++;
    }
  }

  // Check if newBlobs are actually new...
  // First, check if the location is unique, so we don't register new blobs with the same (or similar) coordinates

  else {
    // New blobs must be further away to qualify as new blobs
    float distanceThreshold = 5; 
    // Store new, qualified blobs found in this frame

    PVector p = new PVector();
    for (Contour c : newBlobs) {
      // Get the center coordinate for the new blob
      float x = (float)c.getBoundingBox().getCenterX();
      float y = (float)c.getBoundingBox().getCenterY();
      p.set(x, y);

      // Get existing blob coordinates 
      ArrayList<PVector> coords = new ArrayList<PVector>();
      for (Blob blob : blobList) {
        // Get existing blob coord
        PVector p2 = new PVector();
        p2.x = (float)blob.contour.getBoundingBox().getCenterX();
        p2.y = (float)blob.contour.getBoundingBox().getCenterY();
        coords.add(p2);
      }

      // Check coordinate distance
      boolean isTooClose = false; // Turns true if p.dist
      for (PVector coord : coords) {
        float distance = p.dist(coord);
        if (distance <= distanceThreshold) {
          isTooClose = true;
          break;
        }
      }

      // If none of the existing blobs are too close, add this one to the blob list
      if (!isTooClose) {
        Blob b = new Blob(this, blobCount, c);
        blobCount++;
        blobList.add(b);
      }
    }
  }

  // Update the blob age
  for (int i = 0; i < blobList.size(); i++) {
    Blob b = blobList.get(i);
    b.countDown();
    if (b.dead()) {
      blobList.remove(i); // TODO: Is this safe? Removing from array I'm iterating over...
    }
  }
}

void decodeBlobs() {
  // Decode blobs (a few at a time for now...) 
  int numToDecode = 1;
  if (blobList.size() >= numToDecode) {
    for (int i = 0; i < numToDecode; i++) {
      // Get the blob brightness to determine it's state (HIGH/LOW)
      //println("decoding this blob: "+blobList.get(i).id);
      Rectangle r = blobList.get(i).contour.getBoundingBox();
      PImage cropped = videoInput.get(r.x, r.y, r.width, r.height);
      int br = 0; 
      for (color c : cropped.pixels) {
        br += brightness(c);
      }

      br = br/ cropped.pixels.length;

      if (i == 0) { // Only look at one blob, for now
        blobList.get(i).registerBrightness(br); // Set blob brightness
      }
    }
  }
}
// Filter out contours that are too small or too big
ArrayList<Contour> filterContours(ArrayList<Contour> newContours) {

  ArrayList<Contour> blobs = new ArrayList<Contour>();

  // Which of these contours are blobs?
  for (int i=0; i<newContours.size(); i++) {

    Contour contour = newContours.get(i);
    Rectangle r = contour.getBoundingBox();

    // If contour is too small, don't add blob
    if (r.width < minBlobSize || r.height < minBlobSize || r.width > maxBlobSize || r.height > maxBlobSize) {
      continue;
    }
    blobs.add(contour);
  }

  return blobs;
}

void displayBlobs() {

  for (Blob b : blobList) {
    strokeWeight(1);
    b.display();
  }
}

//void displayContoursBoundingBoxes() {

//  for (int i=0; i<contours.size(); i++) {

//    Contour contour = contours.get(i);
//    Rectangle r = contour.getBoundingBox();

//    if (//(contour.area() > 0.9 * src.width * src.height) ||
//      (r.width < minBlobSize || r.height < minBlobSize))
//      continue;

//    stroke(255, 0, 0);
//    fill(255, 0, 0, 150);
//    strokeWeight(2);
//    rect(r.x, r.y, r.width, r.height);
//  }
//}


// Load file, return success value
boolean loadMovieFile(String path) {
  File f = new File(path);
  if (f.exists()) {
    movie = new Movie(this, "binaryRecording.mp4");
    movie.loop();
  }
  return f.exists();
}

// Movie reading callback
void movieEvent(Movie m) {
  m.read();
}

void saveSVG(ArrayList <PVector> points) {
  if (points.size() == 0) {
    //User is trying to save without anything to output - bail
    println("No point data to save, run mapping first");
    return;
  } else {
    beginRecord(SVG, savePath); 
    for (PVector p : points) {
      point(p.x, p.y);
    }
    endRecord();
    println("SVG saved");
  }

  //selectOutput(prompt, callback, file) - try for file dialog
}

void saveCSV(ArrayList <LED> l, String path) {
  //if (blobList.size() == 0) {
  //  //User is trying to save without anything to output - bail
  //  println("No point data to save, run mapping first");
  //  return;
  //} else {
  PrintWriter output;
  output = createWriter(path); 

  //console feedback
  println("svg contains "+l.size()+" vertecies");

  //write vals out to file, start with csv header
  output.println("address"+","+"x"+","+"y"+","+"z");
  for (int i = 0; i<l.size(); i++) {
    LED temp = l.get(i);
    output.println(temp.address+","+temp.coord.x+","+temp.coord.y+","+temp.coord.z);
    println(temp.address+","+temp.coord.x+","+temp.coord.y+","+temp.coord.z);
  }
  output.close(); // Finishes the file
  println("CSV saved");
  //  }
}

//Filter duplicates from point array
//ArrayList <PVector> removeDuplicates(ArrayList <PVector> points) {
//  println( "Removing duplicates");

//  float thresh = 3.0; 

//  // Iterate through all the points and remove duplicates and 'extra' points (under threshold distance).
//  for (PVector p : points) {
//    float i = points.get(1).dist(p); // distance to current point, used to avoid comporating a point to itself
//    //PVector pt = p;

//    // Do not remove 0,0 points (they're 'invisible' LEDs, we need to keep them).
//    if (p.x == 0 && p.y == 0) {
//      continue; // Go to the next iteration
//    }

//    // Compare point to all other points
//    for (Iterator iter = points.iterator(); iter.hasNext();) {
//      PVector item = (PVector)iter.next();
//      float j = points.get(1).dist(item); 
//      //PVector pt2 = item;
//      float dist = p.dist(item);

//      // Comparing point to itself... do nothing and move on.
//      if (i == j) {
//        //ofLogVerbose("tracking") << "COMPARING POINT TO ITSELF " << pt << endl;
//        continue; // Move on to the next j point
//      }
//      // Duplicate point detection. (This might be covered by the distance check below and therefor redundant...)
//      //else if (pt.x == pt2.x && pt.y == pt2.y) {
//      //  //ofLogVerbose("tracking") << "FOUND DUPLICATE POINT (that is not 0,0) - removing..." << endl;
//      //  iter = points.remove(iter);
//      //  break;
//      //}
//      // Check point distance, remove points that are too close
//      else if (dist < thresh) {
//        println("removing duplicate point");
//        points.remove(iter);
//        break;
//      }
//    }
//  }

//  return points;
//}

//Closes connections (once deployed as applet)
void stop()
{
  cam =null;
  videoExport=null;
  super.stop();
}

//Closes connections
void exit()
{
  cam =null;
  videoExport=null;
  super.exit();
}