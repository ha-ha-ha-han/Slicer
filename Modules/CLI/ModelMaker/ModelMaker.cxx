/*=auto=========================================================================

Portions (c) Copyright 2006 Brigham and Women's Hospital (BWH) All Rights Reserved.

See COPYRIGHT.txt
or http://www.slicer.org/copyright/copyright.txt for details.

Program:   3D Slicer
Module:    $RCSfile$
Date:      $Date$
Version:   $Revision$

=========================================================================auto=*/

#include "ModelMakerCLP.h"

// Slicer includes
#include <vtkPluginFilterWatcher.h>

// MRML includes
#include "vtkMRMLColorTableNode.h"
#include "vtkMRMLColorTableStorageNode.h"
#include "vtkMRMLModelDisplayNode.h"
#include "vtkMRMLModelHierarchyNode.h"
#include "vtkMRMLModelNode.h"
#include "vtkMRMLModelStorageNode.h"
#include "vtkMRMLScene.h"

// vtkITK includes
#include "vtkITKArchetypeImageSeriesScalarReader.h"

// VTK includes
#include <vtkDebugLeaks.h>
#include <vtkDecimatePro.h>
#include <vtkDiscreteMarchingCubes.h>
#include <vtkGeometryFilter.h>
#include <vtkImageAccumulate.h>
#include <vtkImageChangeInformation.h>
#include <vtkImageConstantPad.h>
#include <vtkImageData.h>
#include <vtkImageThreshold.h>
#include <vtkImageToStructuredPoints.h>
#include <vtkInformation.h>
#include <vtkLookupTable.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPolyDataNormals.h>
#include <vtkPolyDataWriter.h>
#include <vtkReverseSense.h>
#include <vtkSmartPointer.h>
#include <vtkSmoothPolyDataFilter.h>
#include <vtkStreamingDemandDrivenPipeline.h>
#include <vtkStripper.h>
#include <vtkThreshold.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkUnstructuredGrid.h>
#include <vtkWindowedSincPolyDataFilter.h>
#include <vtkVersion.h>

// VTKsys includes
#include <vtksys/SystemTools.hxx>

int main(int argc, char * argv[])
{
  PARSE_ARGS;

  vtkDebugLeaks::SetExitError(true);

  if (debug)
    {
    std::cout << "The input volume is: " << InputVolume << std::endl;
    std::cout << "The labels are: " << std::endl;
    for(::size_t l = 0; l < Labels.size(); l++)
      {
      std::cout << "\tList element " << l << " = " << Labels[l] << std::endl;
      }
    std::cout << "The starting label is: " << StartLabel << std::endl;
    std::cout << "The ending label is: " << EndLabel << std::endl;
    std::cout << "The model name is: " << Name << std::endl;
    std::cout << "Do joint smoothing flag is: " << JointSmoothing << std::endl;
    std::cout << "Generate all flag is: " << GenerateAll << std::endl;
    std::cout << "Number of smoothing iterations: " << Smooth << std::endl;
    std::cout << "Number of decimate iterations: " << Decimate << std::endl;
    std::cout << "Split normals? " << SplitNormals << std::endl;
    std::cout << "Calculate point normals? " << PointNormals << std::endl;
    std::cout << "Pad? " << Pad << std::endl;
    std::cout << "Filter type: " << FilterType << std::endl;
    std::cout << "Input color hierarchy scene file: "
              << (ModelHierarchyFile.size() > 0 ? ModelHierarchyFile.c_str() : "None")  << std::endl;
    std::cout << "Output model scene file: "
              << (ModelSceneFile.size() > 0 ? ModelSceneFile[0].c_str() : "None") << std::endl;
    std::cout << "Color table file : " << ColorTable.c_str() << std::endl;
    std::cout << "Save intermediate models: " << SaveIntermediateModels << std::endl;
    std::cout << "Debug: " << debug << std::endl;
    std::cout << "\nStarting..." << std::endl;
    }

  // get the model hierarchy id from the scene file
  std::string::size_type loc;
  std::string            sceneFilename;
  std::string            modelHierarchyID;

  if (InputVolume.size() == 0)
    {
    std::cerr << "ERROR: no input volume defined!" << endl;
    return EXIT_FAILURE;
    }

  if (ModelSceneFile.size() == 0)
    {
    // make one up from the input volume's name
    sceneFilename = vtksys::SystemTools::GetFilenameWithoutExtension(InputVolume) + std::string(".mrml");
    std::cerr << "********\nERROR: no model scene defined! Using " << sceneFilename << endl;
    std::cerr
    <<
    "WARNING: If you started Model Maker from the Slicer3 GUI, the models will NOT be loaded automatically.\nYou must use File->Import Scene "
    << sceneFilename << " to see your models (don't use Load or it will close your current scene).\n*****" << std::endl;
    }
  else
    {
    loc = ModelSceneFile[0].find_last_of("#");
    if (loc != std::string::npos)
      {
      sceneFilename = std::string(ModelSceneFile[0].begin(),
                                  ModelSceneFile[0].begin() + loc);
      loc++;

      modelHierarchyID = std::string(ModelSceneFile[0].begin() + loc, ModelSceneFile[0].end());
      }
    else
      {
      // the passed in file is missing a model hierarchy node, work around it
      sceneFilename = ModelSceneFile[0];
      }
    }

  if (debug)
    {
    std::cout << "Models file: " << sceneFilename << std::endl;
    std::cout << "Model Hierarchy ID: " << modelHierarchyID << std::endl;
    }

  // check for the model mrml file
  if (sceneFilename == "")
    {
    std::cout << "No file to store models!" << std::endl;
    return EXIT_FAILURE;
    }

  // get the directory of the scene file
  std::string rootDir
    = vtksys::SystemTools::GetParentDirectory(sceneFilename.c_str());

  vtkNew<vtkMRMLScene> modelScene;


  // load the scene that Slicer will re-read
  modelScene->SetURL(sceneFilename.c_str());
  // only try importing if the scene file exists
  if (vtksys::SystemTools::FileExists(sceneFilename.c_str()))
    {
    modelScene->Import();

    if (debug)
      {
      std::cout << "Imported model scene file " << sceneFilename.c_str() << std::endl;
      }
    }
  else
    {
    std::cerr << "Model scene file doesn't exist yet: " <<  sceneFilename.c_str() << std::endl;
    }

  // make sure we have a model hierarchy node
  vtkMRMLNode *                              rnd = modelScene->GetNodeByID(modelHierarchyID);
  vtkSmartPointer<vtkMRMLModelHierarchyNode> rtnd;
  if (!rnd)
    {
    std::cerr << "Error: no model hierarchy node at ID \""
              << modelHierarchyID << "\", creating one" << std::endl;
//      return EXIT_FAILURE;
    rtnd = vtkSmartPointer<vtkMRMLModelHierarchyNode>::New();
    rtnd->SetHideFromEditors(0);
    modelScene->AddNode(rtnd);
    // now get it again as a mrml node so can add things under it
    rnd =  modelScene->GetNodeByID(rtnd->GetID());
    }
  else
    {
    if (debug)
      {
      std::cout << "Got model hierarchy node " << rnd->GetID() << std::endl;
      }
    rtnd = vtkMRMLModelHierarchyNode::SafeDownCast(rnd);
    }

  // if there's a color based model hierarchy file, import it into the model scene
  vtkMRMLModelHierarchyNode *topColorHierarchyNode = NULL;
  if (ModelHierarchyFile.length() > 0)
    {
    // only try importing if the scene file exists
    if (vtksys::SystemTools::FileExists(ModelHierarchyFile.c_str()))
      {
      modelScene->SetURL(ModelHierarchyFile.c_str());
      modelScene->Import();
      // reset to the default file name
      modelScene->SetURL(sceneFilename.c_str());
      // and root directory
      modelScene->SetRootDirectory(rootDir.c_str());
      if (debug)
        {
        std::cout << "Imported model hierarchy scene file " << ModelHierarchyFile.c_str() << std::endl;
        }
      }
    else
      {
      std::cerr << "Model hierarchy scene file doesn't exist, using a flat hieararchy" << std::endl;
      }

    // make sure we have a new model hierarchy node
    vtkMRMLNode * mnode = modelScene->GetNthNodeByClass(1,"vtkMRMLModelHierarchyNode");
    if (mnode != NULL)
      {
      topColorHierarchyNode = vtkMRMLModelHierarchyNode::SafeDownCast(mnode);
      }
    if (!topColorHierarchyNode)
      {
      std::cerr << "Model hierarchy scene file failed to import a model hierarchy node" << std::endl;
      }
    else
      {
      if (debug)
        {
        std::cout << "Loaded a color based model hierarchy scene with top level node = " << topColorHierarchyNode->GetName() << ", id = " << topColorHierarchyNode->GetID() << std::endl;
        }
      }
    }


  // if have a color hiearchy node, make it a child of the passed in model hiearchy
  if (topColorHierarchyNode != NULL)
    {
    topColorHierarchyNode->SetParentNodeID(rtnd->GetID());
    // there's also a chance that the parent node refs weren't reset when the top color hierarchy node was re-id'd
    // go through all the hierarchy nodes that are right under the rtnd and reset them to be under the topColorHierarchyNode
    std::vector< vtkMRMLHierarchyNode* > children = rtnd->GetChildrenNodes();
    for (unsigned int i = 0; i < children.size(); i++)
      {
      // don't touch the top color hierarchy node since you don't want to reset it's parent to itself
      if (strcmp(topColorHierarchyNode->GetID(), children[i]->GetID()) != 0)
        {
        children[i]->SetParentNodeID(topColorHierarchyNode->GetID());
        if (debug)
          {
          std::cout << "Reset child " << i << " " << children[i]->GetName() << " so parent is now " << children[i]->GetParentNodeID() << std::endl;
          }
        }
      }
    }

  vtkSmartPointer<vtkMRMLColorTableNode>        colorNode;
  vtkSmartPointer<vtkMRMLColorTableStorageNode> colorStorageNode;

  int useColorNode = 0;
  if (ColorTable !=  "")
    {
    useColorNode = 1;
    }

  // vtk and helper variables
  vtkSmartPointer<vtkITKArchetypeImageSeriesReader> reader;
  vtkImageData *                                    image;
  vtkSmartPointer<vtkDiscreteMarchingCubes>         cubes;
  vtkSmartPointer<vtkWindowedSincPolyDataFilter>    smoother;
  bool                                              makeMultiple = false;
  bool                                              useStartEnd = false;
  vtkSmartPointer<vtkImageAccumulate>               hist;
  std::vector<int>                                  skippedModels;
  std::vector<int>                                  madeModels;
  vtkSmartPointer<vtkWindowedSincPolyDataFilter>    smootherSinc;
  vtkSmartPointer<vtkSmoothPolyDataFilter>          smootherPoly;

  vtkSmartPointer<vtkImageConstantPad>        padder;
  vtkSmartPointer<vtkDecimatePro>             decimator;
  vtkSmartPointer<vtkMarchingCubes>           mcubes;
  vtkSmartPointer<vtkImageThreshold>          imageThreshold;
  vtkSmartPointer<vtkThreshold>               threshold;
  vtkSmartPointer<vtkImageToStructuredPoints> imageToStructuredPoints;
  vtkSmartPointer<vtkGeometryFilter>          geometryFilter;
  vtkSmartPointer<vtkTransform>               transformIJKtoRAS;
  vtkSmartPointer<vtkReverseSense>            reverser;
  vtkSmartPointer<vtkTransformPolyDataFilter> transformer;
  vtkSmartPointer<vtkPolyDataNormals>         normals;
  vtkSmartPointer<vtkStripper>                stripper;
  vtkSmartPointer<vtkPolyDataWriter>          writer;

  // keep track of number of models that will be generated, for filter
  // watcher reporting
  float numModelsToGenerate = 1.0;
  float numSingletonFilterSteps;
  float numRepeatedFilterSteps;
  float numFilterSteps;
  // increment after each filter is run
  float currentFilterOffset = 0.0;
  // if using the labels vector, get out the min/max
  int labelsMin = 0, labelsMax = 0;

  // figure out if we're making multiple models
  if (GenerateAll)
    {
    makeMultiple = true;
    if (debug)
      {
      std::cout << "GenerateAll! set make mult to true" << std::endl;
      }
    }
  else if (Labels.size() > 0)
    {
    if (Labels.size() == 1)
      {
      // special case, only making one
      labelsMin = Labels[0];
      labelsMax = Labels[0];
      }
    else
      {
      makeMultiple = true;
      // sort the vector
      std::sort(Labels.begin(), Labels.end());
      labelsMin = Labels[0];
      labelsMax = Labels[Labels.size() - 1];
      if (debug)
        {
        cout << "Set labels min to " << labelsMin << ", labels max = " << labelsMax << ", labels vector size = "
             << Labels.size() << endl;
        }
      }
    }
  else if (EndLabel >= StartLabel && (EndLabel != -1 && StartLabel != -1))
    {
    // only say we're making multiple if they're not the same
    if (EndLabel > StartLabel)
      {
      makeMultiple = true;
      }
    useStartEnd = true;
    }

  if (makeMultiple)
    {
    numSingletonFilterSteps = 4;
    if (JointSmoothing)
      {
      numRepeatedFilterSteps = 7;
      }
    else
      {
      numRepeatedFilterSteps = 9;
      }
    if (useStartEnd)
      {
      numModelsToGenerate = EndLabel - StartLabel + 1;
      }
    else
      {
      if (GenerateAll)
        {
        // this will be calculated later from the histogram output
        }
      else
        {
        numModelsToGenerate = Labels.size();
        }
      }
    }
  else
    {
    numSingletonFilterSteps = 1;
    numRepeatedFilterSteps = 9;
    }
  numFilterSteps = numSingletonFilterSteps + (numRepeatedFilterSteps * numModelsToGenerate);
  if (SaveIntermediateModels)
    {
    if (JointSmoothing)
      {
      numFilterSteps += numModelsToGenerate;
      }
    else
      {
      numFilterSteps += 3 * numModelsToGenerate;
      }
    }

  if (debug)
    {
    std::cout << "useStartEnd = " << useStartEnd << ", numModelsToGenerate = " << numModelsToGenerate
              << ", numFilterSteps " << numFilterSteps << endl;
    }
  // check for the input file
  // - strings that start with slicer: are shared memory references, so they won't exist.
  //   The memory address starts with 0x in linux but not on Windows
  if (InputVolume.find(std::string("slicer:")) != 0)
    {
    FILE * infile;
    infile = fopen(InputVolume.c_str(), "r");
    if (infile == NULL)
      {
      std::cerr << "ERROR: cannot open input volume file " << InputVolume << endl;
      return EXIT_FAILURE;
      }
    fclose(infile);
    }

  // Read the file
  reader = vtkSmartPointer<vtkITKArchetypeImageSeriesScalarReader>::New();
  std::string            comment = "Read Volume";
  vtkPluginFilterWatcher watchReader(reader,
                                     comment.c_str(),
                                     CLPProcessInformation,
                                     1.0 / numFilterSteps, currentFilterOffset / numFilterSteps);
  if (debug)
    {
    watchReader.QuietOn();
    }
  currentFilterOffset++;
  reader->SetArchetype(InputVolume.c_str());
  reader->SetOutputScalarTypeToNative();
  reader->SetDesiredCoordinateOrientationToNative();
  reader->SetUseNativeOriginOn();
  reader->Update();
  vtkNew<vtkImageChangeInformation> ici;
#if (VTK_MAJOR_VERSION <= 5)
  ici->SetInput(reader->GetOutput());
#else
  ici->SetInputConnection(reader->GetOutputPort());
#endif
  ici->SetOutputSpacing(1, 1, 1);
  ici->SetOutputOrigin(0, 0, 0);
  ici->Update();

  image = ici->GetOutput();
#if (VTK_MAJOR_VERSION <= 5)
  image->Update();
#else
  ici->Update();
#endif

  // add padding if flag is set
  if (Pad)
    {
    std::cout << "Adding 1 pixel padding around the image, shifting origin." << std::endl;
    if (padder)
      {
#if (VTK_MAJOR_VERSION <= 5)
      padder->SetInput(NULL);
#else
      padder->SetInputData(NULL);
#endif
      padder = NULL;
      }
    padder = vtkSmartPointer<vtkImageConstantPad>::New();
    vtkNew<vtkImageChangeInformation> translator;
#if (VTK_MAJOR_VERSION <= 5)
    translator->SetInput(image);
#else
    translator->SetInputData(image);
#endif
    // translate the extent by 1 pixel
    translator->SetExtentTranslation(1, 1, 1);
    // args are: -padx*xspacing, -pady*yspacing, -padz*zspacing
    // but padding and spacing are both 1
    translator->SetOriginTranslation(-1.0, -1.0, -1.0);
#if (VTK_MAJOR_VERSION <= 5)
    padder->SetInput(translator->GetOutput());
#else
    padder->SetInputConnection(translator->GetOutputPort());
#endif
    padder->SetConstant(0);

    translator->Update();
    int extent[6];
#if (VTK_MAJOR_VERSION <= 5)
    image->GetWholeExtent(extent);
#else
    ici->GetOutputInformation(0)->Get(
      vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), extent);
#endif
    // now set the output extent to the new size, padded by 2 on the
    // positive side
    padder->SetOutputWholeExtent(extent[0], extent[1] + 2,
                                 extent[2], extent[3] + 2,
                                 extent[4], extent[5] + 2);
    }
  if (useColorNode)
    {
    colorNode = vtkSmartPointer<vtkMRMLColorTableNode>::New();
    modelScene->AddNode(colorNode);

    // read the colour file
    if (debug)
      {
      std::cout << "Colour table file name = " << ColorTable.c_str() << std::endl;
      }
    colorStorageNode = vtkSmartPointer<vtkMRMLColorTableStorageNode>::New();
    colorStorageNode->SetFileName(ColorTable.c_str());
    modelScene->AddNode(colorStorageNode);

    if (debug)
      {
      std::cout << "Setting the colour node's storage node id to " << colorStorageNode->GetID()
                << ", it's file name = " << colorStorageNode->GetFileName() << std::endl;
      }
    colorNode->SetAndObserveStorageNodeID(colorStorageNode->GetID());
    if (!colorStorageNode->ReadData(colorNode))
      {
      std::cerr << "Error reading colour file " << colorStorageNode->GetFileName() << endl;
      return EXIT_FAILURE;
      }
    else
      {
      if (debug)
        {
        std::cout << "Read colour file  " << colorStorageNode->GetFileName() << endl;
        }
      }
    colorStorageNode = NULL;
    }

  // each hierarchy node needs a display node
  vtkNew<vtkMRMLModelDisplayNode> dnd;
  dnd->SetVisibility(1);
  modelScene->AddNode(dnd.GetPointer());
  rtnd->SetAndObserveDisplayNodeID(dnd->GetID());

  // If making mulitple models, figure out which labels have voxels
  if (makeMultiple)
    {
    hist = vtkSmartPointer<vtkImageAccumulate>::New();
#if (VTK_MAJOR_VERSION <= 5)
    hist->SetInput(image);
#else
    hist->SetInputData(image);
#endif
    // need to figure out how many bins
    int extentMax = 0;
    if (useColorNode)
      {
      // get the max integer that the colour node can map
      extentMax = colorNode->GetNumberOfColors() - 1;
      if (debug)
        {
        std::cout << "Using color node to get max label" << endl;
        }
      }
    else
      {
      // use the full range of the scalar type
      double dImageScalarMax = image->GetScalarTypeMax();
      if (debug)
        {
        std::cout << "Image scalar max as double = " << dImageScalarMax << endl;
        }
      extentMax = (int)(floor(dImageScalarMax - 1.0));
      int biggestBin = 1000000;     // VTK_INT_MAX - 1;
      if (extentMax < 0 || extentMax > biggestBin)
        {
        std::cout << "\nWARNING: due to lack of color label information and an image with a scalar maximum of "
                  << dImageScalarMax << ", using  " << biggestBin << " as the histogram number of bins" << endl;
        extentMax = biggestBin;
        }
      else
        {
        std::cout
        <<
        "\nWARNING: due to lack of color label information, using the full scalar range of the input image when calculating the histogram over the image: "
        << extentMax << endl;
        }
      }
    if (debug)
      {
      std::cout << "Setting histogram extentMax = " << extentMax << endl;
      }
    // hist->SetComponentExtent(0, 1023, 0, 0, 0, 0);
    hist->SetComponentExtent(0, extentMax, 0, 0, 0, 0);
    hist->SetComponentOrigin(0, 0, 0);
    hist->SetComponentSpacing(1, 1, 1);
    // try and update and get the min/max here, as need them for the
    // marching cubes
    comment = "Histogram All Models";
    vtkPluginFilterWatcher watchImageAccumulate(hist,
                                                comment.c_str(),
                                                CLPProcessInformation,
                                                1.0 / numFilterSteps,
                                                currentFilterOffset / numFilterSteps);
    currentFilterOffset += 1.0;
    if (debug)
      {
      watchImageAccumulate.QuietOn();
      }
    hist->Update();
    double *max = hist->GetMax();
    double *min = hist->GetMin();
    if (min[0] == 0)
      {
      if (debug)
        {
        std::cout << "Skipping 0" << endl;
        }
      min[0]++;
      }
    if (min[0] < 0)
      {
      if (debug)
        {
        std::cout << "Histogram min was less than zero: " << min[0] << ", resetting to 1\n";
        }
      min[0] = 1;
      }

    if (debug)
      {
      std::cout << "Hist: Min = " << min[0] << " and max = " << max[0] << " (image scalar type = "
                << image->GetScalarType() << ", max = " << image->GetScalarTypeMax() << ")" << endl;
      }
    if (GenerateAll)
      {
      if (debug)
        {
        std::cout << "GenerateAll flag is true, resetting the start and end labels from: " << StartLabel << " and "
                  << EndLabel << " to " << min[0] << " and " << max[0] << endl;
        }
      StartLabel = (int)floor(min[0]);
      EndLabel = (int)floor(max[0]);
      useStartEnd = true;
      // recalculate the number of filter steps, discount the labels with no
      // voxels
      numModelsToGenerate = 0;
      for(int i = StartLabel; i <= EndLabel; i++)
        {
        if ((int)floor((((hist->GetOutput())->GetPointData())->GetScalars())->GetTuple1(i)) > 0)
          {
          if (debug && i < 0 && i > -100)
            {
            std::cout << i << " ";
            }
          numModelsToGenerate++;
          }
        }
      if (debug)
        {
        std::cout << endl << "GenerateAll: there are " << numModelsToGenerate << " models to be generated." << endl;
        }
      numFilterSteps = numSingletonFilterSteps + (numRepeatedFilterSteps * numModelsToGenerate);
      if (SaveIntermediateModels)
        {
        if (JointSmoothing)
          {
          // save after decimation
          numFilterSteps += 1 * numModelsToGenerate;
          }
        else
          {
          // if not doing joint smoothing, save after marching cubes and
          // smoothing as well as decimation
          numFilterSteps += 3 * numModelsToGenerate;
          }
        }
      if (debug)
        {
        std::cout << "Reset numFilterSteps to " << numFilterSteps << endl;
        }
      }

    if (cubes)
      {
#if (VTK_MAJOR_VERSION <= 5)
      cubes->SetInput(NULL);
#else
      cubes->SetInputData(NULL);
#endif
      cubes = NULL;
      }
    cubes = vtkSmartPointer<vtkDiscreteMarchingCubes>::New();
    std::string            comment1 = "Discrete Marching Cubes";
    vtkPluginFilterWatcher watchDMCubes(cubes,
                                        comment1.c_str(),
                                        CLPProcessInformation,
                                        1.0 / numFilterSteps,
                                        currentFilterOffset / numFilterSteps);
    if (debug)
      {
      watchDMCubes.QuietOn();
      }
    currentFilterOffset += 1.0;
    // add padding if flag is set
    if (Pad)
      {
#if (VTK_MAJOR_VERSION <= 5)
      cubes->SetInput(padder->GetOutput());
#else
      cubes->SetInputConnection(padder->GetOutputPort());
#endif
      }
    else
      {
#if (VTK_MAJOR_VERSION <= 5)
      cubes->SetInput(image);
#else
      cubes->SetInputData(image);
#endif
      }
    if (useStartEnd)
      {
      if (debug)
        {
        std::cout << "Marching cubes: Using end label = " << EndLabel << ", start label = " << StartLabel << endl;
        }
      cubes->GenerateValues((EndLabel - StartLabel + 1), StartLabel, EndLabel);
      }
    else
      {
      if (debug)
        {
        std::cout << "Marching cubes: Using max = " << labelsMax << ", min = " << labelsMin << endl;
        }
      cubes->GenerateValues((labelsMax - labelsMin + 1), labelsMin, labelsMax);
      }
    try
      {
      cubes->Update();
      }
    catch(...)
      {
      std::cerr << "ERROR while updating marching cubes filter." << std::endl;
      return EXIT_FAILURE;
      }
    if (JointSmoothing)
      {
      float passBand = 0.001;
      if (smoother)
        {
#if (VTK_MAJOR_VERSION <= 5)
        smoother->SetInput(NULL);
#else
        smoother->SetInputData(NULL);
#endif
        smoother = NULL;
        }
      smoother = vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
      std::stringstream stream;
      stream << "Joint Smooth All Models (";
      stream << numModelsToGenerate;
      stream << " to process)";
      std::string            comment2 = stream.str();
      vtkPluginFilterWatcher watchSmoother(smoother,
                                           comment2.c_str(),
                                           CLPProcessInformation,
                                           1.0 / numFilterSteps,
                                           currentFilterOffset / numFilterSteps);
      currentFilterOffset += 1.0;
      if (debug)
        {
        watchSmoother.QuietOn();
        }
      cubes->ReleaseDataFlagOn();
#if (VTK_MAJOR_VERSION <= 5)
      smoother->SetInput(cubes->GetOutput());
#else
      smoother->SetInputConnection(cubes->GetOutputPort());
#endif
      smoother->SetNumberOfIterations(Smooth);
      smoother->BoundarySmoothingOff();
      smoother->FeatureEdgeSmoothingOff();
      smoother->SetFeatureAngle(120.0l);
      smoother->SetPassBand(passBand);
      smoother->NonManifoldSmoothingOn();
      smoother->NormalizeCoordinatesOn();

      try
        {
        smoother->Update();
        }
      catch(...)
        {
        std::cerr << "ERROR while updating smoothing filter." << std::endl;
        return EXIT_FAILURE;
        }
      //        smoother->ReleaseDataFlagOn();
      }
/*
      vtkPluginFilterWatcher watchImageAccumulate(hist,
                                                 "Histogram All Models",
                                                 CLPProcessInformation,
                                                 1.0/numFilterSteps,
                                                 currentFilterOffset/numFilterSteps);
      currentFilterOffset += 1.0;
      if (debug)
        {
        watchImageAccumulate.QuietOn();
        }
      hist->Update();
      double *max = hist->GetMax();
      double *min = hist->GetMin();
      if (min[0] == 0)
        {
        if (debug)
          {
          std::cout << "Skipping 0" << endl;
          }
        min[0]++;
        }
       if (debug)
         {
         std::cout << "Min = " << min[0] << " and max = " << max[0] << endl;
         }

      if (GenerateAll)
        {
        if (debug)
          {
          std::cout << "GenerateAll flag is true, resetting the start and end labels from: " << StartLabel << " and " << EndLabel << " to " << min[0] << " and " << max[0] << endl;
          }
        StartLabel = (int)floor(min[0]);
        EndLabel = (int)floor(max[0]);
        // recalculate the number of filter steps, discount the labels with no
        // voxels
        numModelsToGenerate = 0;
        for (int i = StartLabel; i <= EndLabel; i++)
          {
          if((int)floor((((hist->GetOutput())->GetPointData())->GetScalars())->GetTuple1(i)) > 0)
            {
            if (debug && i < 0 && i > -100) { std::cout << i << " "; }
            numModelsToGenerate++;
            }
          }
        if (debug)
          {
          std::cout << endl << "GenerateAll: there are " << numModelsToGenerate << " models to be generated." << endl;
          }
        numFilterSteps = numSingletonFilterSteps + (numRepeatedFilterSteps * numModelsToGenerate);
        }
*/
    if (useColorNode)
      {
      // but if we didn't get a named color node, try to guess
      if (colorNode == NULL)
        {
        std::cerr << "ERROR: must have a color node! Should be associated with the input label map volume.\n";
        return EXIT_FAILURE;
        }
      }
    }   // end of make multiple
  else
    {
    if (useStartEnd)
      {
      EndLabel = StartLabel;
      }
    }

  // ModelMakerMarch
  double      labelFrequency = 0.0;;
  std::string labelName;

  // get the dimensions, marching cubes only works on 3d
  int extents[6];
  image->GetExtent(extents);
  if (debug)
    {
    std::cout << "Image data extents: " << extents[0] << " " << extents[1] << " " << extents[2] << " " << extents[3]
              << " " << extents[4] << " " << extents[5] << endl;
    }
  if (extents[0] == extents[1] ||
      extents[2] == extents[3] ||
      extents[4] == extents[5])
    {
    std::cerr << "The volume is not 3D." << endl;
    std::cerr << "\tImage data extents: " << extents[0] << " " << extents[1] << " " << extents[2] << " "
              << extents[3] << " " << extents[4] << " " << extents[5] << endl;
    return EXIT_FAILURE;
    }
  // Get the RAS to IJK matrix and invert it to get the IJK to RAS which will need
  // to be applied to the model as it will be built in pixel space
  if (transformIJKtoRAS)
    {
    transformIJKtoRAS->SetInput(NULL);
    transformIJKtoRAS = NULL;
    }
  transformIJKtoRAS = vtkSmartPointer<vtkTransform>::New();
  transformIJKtoRAS->SetMatrix(reader->GetRasToIjkMatrix());
  if (debug)
    {
    // std::cout << "RasToIjk matrix from file = ";
    // transformIJKtoRAS->GetMatrix()->Print(std::cout);
    // std::cout << endl;
    }
  transformIJKtoRAS->Inverse();

  //
  // Loop through all the labels
  //
  std::vector<int> loopLabels;
  if (useStartEnd || GenerateAll)
    {
    // set up the loop list with all the labels between start and end
    for(int i = StartLabel; i <= EndLabel; i++)
      {
      loopLabels.push_back(i);
      }
    }
  else
    {
    // just copy the list of labels into the new var
    for(::size_t i = 0; i < Labels.size(); i++)
      {
      loopLabels.push_back(Labels[i]);
      }
    }
  for(::size_t l = 0; l < loopLabels.size(); l++)
    {
    // get the label out of the vector
    int i = loopLabels[l];

    if (makeMultiple)
      {
      labelFrequency = (((hist->GetOutput())->GetPointData())->GetScalars())->GetTuple1(i);
      if (debug)
        {
        if (labelFrequency > 0.0)
          {
          std::cout << "Label    " << i << " has " << labelFrequency << " voxels." << endl;
          }
        }
      if (labelFrequency == 0.0)
        {
        skippedModels.push_back(i);
        continue;
        }
      else
        {
        madeModels.push_back(i);
        }

      // name this model
      // TODO: get the label name from the colour look up table
      std::stringstream stream;
      stream <<    i;
      std::string stringI =    stream.str();
      if (colorNode != NULL)
        {
        std::string colorName = std::string(colorNode->GetColorNameAsFileName(i));
        if (colorName.c_str() != NULL)
          {
          if (!SkipUnNamed ||
              (SkipUnNamed && (colorName.compare("invalid") != 0 && colorName.compare("(none)") != 0)))
            {
            labelName = Name + std::string("_") + stringI + std::string("_") + colorName;
            if (debug)
              {
              std::cout << "Got color name, set label name = " << labelName.c_str() << " (color name w/o spaces = "
                        << colorName.c_str() << ")" << endl;
              }
            }
          else
            {
            if (debug)
              {
              std::cout << "Invalid colour name for " << stringI.c_str() << " = " << colorName.c_str()
                        << ", skipping.\n";
              }
            skippedModels.push_back(i);
            madeModels.pop_back();
            continue;
            }
          }
        else
          {
          if (SkipUnNamed)
            {
            if (debug)
              {
              std::cout << "Null color name for " << i << endl;
              }
            skippedModels.push_back(i);
            madeModels.pop_back();
            continue;
            }
          else
            {
            // colour is out of range
            labelName = Name + std::string("_") + stringI;
            }
          }
        }
      else
        {
        if (!SkipUnNamed)
          {
          labelName  = Name + std::string("_") + stringI;
          }
        else
          {
          continue;
          }
        }
      }   // end of making multiples
    else
      {
      // just make one
      labelName = Name;
      /*
      if (colorNode != NULL)
        {
        if (colorNode->GetColorNameAsFileName(i).c_str() != NULL)
          {
          std::stringstream    stream;
          stream <<    i;
          std::string stringI =    stream.str();
          labelName = Name + std::string("_") + stringI + std::string("_") + std::string(colorNode->GetColorNameAsFileName(i));
          }
        }
      else
        {
        labelName = Name;
        }
      */
      }

    // threshold
    if (JointSmoothing == 0)
      {
      if (imageThreshold)
        {
#if (VTK_MAJOR_VERSION <= 5)
        imageThreshold->SetInput(NULL);
#else
        imageThreshold->SetInputData(NULL);
#endif
        imageThreshold->RemoveAllInputs();
        imageThreshold = NULL;
        }
      imageThreshold = vtkSmartPointer<vtkImageThreshold>::New();
      std::string            comment3 = "Threshold " + labelName;
      vtkPluginFilterWatcher watchImageThreshold(imageThreshold,
                                                 comment3.c_str(),
                                                 CLPProcessInformation,
                                                 1.0 / numFilterSteps,
                                                 currentFilterOffset / numFilterSteps);
      currentFilterOffset += 1.0;
      if (debug)
        {
        watchImageThreshold.QuietOn();
        }
      if (Pad)
        {
#if (VTK_MAJOR_VERSION <= 5)
        imageThreshold->SetInput(padder->GetOutput());
#else
        imageThreshold->SetInputConnection(padder->GetOutputPort());
#endif
        }
      else
        {
#if (VTK_MAJOR_VERSION <= 5)
        imageThreshold->SetInput(image);
#else
        imageThreshold->SetInputData(image);
#endif
        }
      imageThreshold->SetReplaceIn(1);
      imageThreshold->SetReplaceOut(1);
      imageThreshold->SetInValue(200);
      imageThreshold->SetOutValue(0);

      imageThreshold->ThresholdBetween(i, i);
#if (VTK_MAJOR_VERSION <= 5)
      (imageThreshold->GetOutput())->ReleaseDataFlagOn();
#endif
      imageThreshold->ReleaseDataFlagOn();

      if (imageToStructuredPoints)
        {
#if (VTK_MAJOR_VERSION <= 5)
        imageToStructuredPoints->SetInput(NULL);
#else
        imageToStructuredPoints->SetInputData(NULL);
#endif
        imageToStructuredPoints = NULL;
        }
      imageToStructuredPoints = vtkSmartPointer<vtkImageToStructuredPoints>::New();
#if (VTK_MAJOR_VERSION <= 5)
      imageToStructuredPoints->SetInput(imageThreshold->GetOutput());
#else
      imageToStructuredPoints->SetInputConnection(imageThreshold->GetOutputPort());
#endif
      try
        {
        imageToStructuredPoints->Update();
        }
      catch(...)
        {
        std::cerr << "ERROR while updating image to structured points for label " << i << std::endl;
        return EXIT_FAILURE;
        }
      imageToStructuredPoints->ReleaseDataFlagOn();
      }
    else
      {
      // use the output of the smoother
      if (threshold)
        {
#if (VTK_MAJOR_VERSION <= 5)
        threshold->SetInput(NULL);
#else
        threshold->SetInputData(NULL);
#endif
        threshold = NULL;
        }
      threshold = vtkSmartPointer<vtkThreshold>::New();
      std::string            comment4 = "Threshold " + labelName;
      vtkPluginFilterWatcher watchThreshold(threshold,
                                            comment4.c_str(),
                                            CLPProcessInformation,
                                            1.0 / numFilterSteps,
                                            currentFilterOffset / numFilterSteps);
      currentFilterOffset += 1.0;
      if (debug)
        {
        watchThreshold.QuietOn();
        }
      if (smoother == NULL)
        {
        std::cerr << "\nERROR smoothing filter is null for joint smoothing!" << std::endl;
        return EXIT_FAILURE;
        }
#if (VTK_MAJOR_VERSION <= 5)
      threshold->SetInput(smoother->GetOutput());
#else
      threshold->SetInputConnection(smoother->GetOutputPort());
#endif
      // In VTK 5.0, this is deprecated - the default behaviour seems to
      // be okay
      // threshold->SetAttributeModeToUseCellData();

      threshold->ThresholdBetween(i, i);
#if (VTK_MAJOR_VERSION <= 5)
      (threshold->GetOutput())->ReleaseDataFlagOn();
#endif
      threshold->ReleaseDataFlagOn();

      if (geometryFilter)
        {
#if (VTK_MAJOR_VERSION <= 5)
        geometryFilter->SetInput(NULL);
#else
        geometryFilter->SetInputData(NULL);
#endif
        geometryFilter = NULL;
        }
      geometryFilter = vtkSmartPointer<vtkGeometryFilter>::New();
#if (VTK_MAJOR_VERSION <= 5)
      geometryFilter->SetInput(threshold->GetOutput());
#else
      geometryFilter->SetInputConnection(threshold->GetOutputPort());
#endif
      geometryFilter->ReleaseDataFlagOn();
      }

    // if not joint smoothing, may need to skip this label
    int skipLabel = 0;
    if (JointSmoothing == 0)
      {
      if (mcubes)
        {
#if (VTK_MAJOR_VERSION <= 5)
        mcubes->SetInput(NULL);
#else
        mcubes->SetInputData(NULL);
#endif
        mcubes = NULL;
        }
      mcubes = vtkSmartPointer<vtkMarchingCubes>::New();
      std::string            comment5 = "Marching Cubes " + labelName;
      vtkPluginFilterWatcher watchThreshold(mcubes,
                                            comment5.c_str(),
                                            CLPProcessInformation,
                                            1.0 / numFilterSteps,
                                            currentFilterOffset / numFilterSteps);
      currentFilterOffset += 1.0;
      if (debug)
        {
        watchThreshold.QuietOn();
        }
#if (VTK_MAJOR_VERSION <= 5)
      mcubes->SetInput(imageToStructuredPoints->GetOutput());
#else
      mcubes->SetInputConnection(imageToStructuredPoints->GetOutputPort());
#endif
      mcubes->SetValue(0, 100.5);
      mcubes->ComputeScalarsOff();
      mcubes->ComputeGradientsOff();
      mcubes->ComputeNormalsOff();
#if (VTK_MAJOR_VERSION <= 5)
      (mcubes->GetOutput())->ReleaseDataFlagOn();
#else
      mcubes->ReleaseDataFlagOn();
#endif
      try
        {
        mcubes->Update();
        }
      catch(...)
        {
        std::cerr << "ERROR while running marching cubes, for label " << i << std::endl;
        return EXIT_FAILURE;
        }
      if (debug)
        {
        std::cout << "\nNumber of polygons = " << (mcubes->GetOutput())->GetNumberOfPolys() << endl;
        }

      if ((mcubes->GetOutput())->GetNumberOfPolys()  == 0)
        {
        std::cout << "Cannot create a model from label " << i
                  << "\nNo polygons can be created,\nthere may be no voxels with this label in the volume." << endl;
        if (transformIJKtoRAS)
          {
          transformIJKtoRAS = NULL;
          }
        if (imageThreshold)
          {
          if (debug)
            {
            std::cout << "Setting image threshold input to null" << endl;
            }
#if (VTK_MAJOR_VERSION <= 5)
          imageThreshold->SetInput(NULL);
#else
          imageThreshold->SetInputData(NULL);
#endif
          imageThreshold->RemoveAllInputs();
          imageThreshold = NULL;

          }
        if (imageToStructuredPoints)
          {
#if (VTK_MAJOR_VERSION <= 5)
          imageToStructuredPoints->SetInput(NULL);
#else
          imageToStructuredPoints->SetInputData(NULL);
#endif
          imageToStructuredPoints = NULL;
          }
        if (mcubes)
          {
#if (VTK_MAJOR_VERSION <= 5)
          mcubes->SetInput(NULL);
#else
          mcubes->SetInputData(NULL);
#endif
          mcubes = NULL;
          }
        skipLabel = 1;
        std::cout << "...continuing" << endl;
        continue;
        }
      if (SaveIntermediateModels)
        {
        writer = vtkSmartPointer<vtkPolyDataWriter>::New();
        std::string            commentSaveCubes = "Writing intermediate model after marching cubes " + labelName;
        vtkPluginFilterWatcher watchWriter(writer,
                                           commentSaveCubes.c_str(),
                                           CLPProcessInformation,
                                           1.0 / numFilterSteps,
                                           currentFilterOffset / numFilterSteps);
        currentFilterOffset += 1.0;
#if (VTK_MAJOR_VERSION <= 5)
        writer->SetInput(cubes->GetOutput());
#else
        writer->SetInputConnection(cubes->GetOutputPort());
#endif
        writer->SetFileType(2);
        std::string fileName;
        if (rootDir != "")
          {
          fileName = rootDir + std::string("/") + labelName + std::string("-MarchingCubes.vtk");
          }
        else
          {
          fileName = labelName + std::string("-MarchingCubes.vtk");
          }
        if (debug)
          {
          watchWriter.QuietOn();
          std::cout << "Writing intermediate file " << fileName.c_str() << std::endl;
          }
        writer->SetFileName(fileName.c_str());
        if (!writer->Write())
          {
          std::cerr << "ERROR: Failed to write intermediate file " << fileName.c_str() << std::endl;
          }
#if (VTK_MAJOR_VERSION <= 5)
        writer->SetInput(NULL);
#else
        writer->SetInputData(NULL);
#endif
        writer = NULL;
        }
      }
    else
      {
      std::cout << "Skipping marching cubes..." << endl;
      }
    if (!skipLabel)
      {
      // In switch from vtk 4 to vtk 5, vtkDecimate was deprecated from the Patented dir, use vtkDecimatePro
      // TODO: look at vtkQuadraticDecimation
      if (decimator != NULL)
        {
#if (VTK_MAJOR_VERSION <= 5)
        decimator->SetInput(NULL);
#else
        decimator->SetInputData(NULL);
#endif
        decimator = NULL;
        }
      decimator = vtkSmartPointer<vtkDecimatePro>::New();
      std::string            comment6 = "Decimate " + labelName;
      vtkPluginFilterWatcher watchImageThreshold(decimator,
                                                 comment6.c_str(),
                                                 CLPProcessInformation,
                                                 1.0 / numFilterSteps,
                                                 currentFilterOffset / numFilterSteps);
      currentFilterOffset += 1.0;
      if (debug)
        {
        watchImageThreshold.QuietOn();
        }
      if (JointSmoothing == 0)
        {
#if (VTK_MAJOR_VERSION <= 5)
        decimator->SetInput(mcubes->GetOutput());
#else
        decimator->SetInputConnection(mcubes->GetOutputPort());
#endif
        }
      else
        {
#if (VTK_MAJOR_VERSION <= 5)
        decimator->SetInput(geometryFilter->GetOutput());
#else
        decimator->SetInputConnection(geometryFilter->GetOutputPort());
#endif
        }
      decimator->SetFeatureAngle(60);
      // decimator->SetMaximumIterations(Decimate);
      // decimator->SetMaximumSubIterations(0);

      // decimator->PreserveEdgesOn();
      decimator->SplittingOff();
      decimator->PreserveTopologyOn();

      decimator->SetMaximumError(1);
      decimator->SetTargetReduction(Decimate);
      // decimator->SetInitialError(0.0002);
      // decimator->SetErrorIncrement(0.002);
#if (VTK_MAJOR_VERSION <= 5)
      (decimator->GetOutput())->ReleaseDataFlagOff();
#else
      decimator->ReleaseDataFlagOff();
#endif

      try
        {
        decimator->Update();
        }
      catch(...)
        {
        std::cerr << "ERROR decimating model " << i << std::endl;
        return EXIT_FAILURE;
        }
      if (debug)
        {
        std::cout << "After decimation, number of polygons = " << (decimator->GetOutput())->GetNumberOfPolys() << endl;
        }

      if (SaveIntermediateModels)
        {
        writer = vtkSmartPointer<vtkPolyDataWriter>::New();
        std::string            commentSaveDecimation = "Writing intermediate model after decimation " + labelName;
        vtkPluginFilterWatcher watchWriter(writer,
                                           commentSaveDecimation.c_str(),
                                           CLPProcessInformation,
                                           1.0 / numFilterSteps,
                                           currentFilterOffset / numFilterSteps);
        currentFilterOffset += 1.0;
#if (VTK_MAJOR_VERSION <= 5)
        writer->SetInput(decimator->GetOutput());
#else
        writer->SetInputConnection(decimator->GetOutputPort());
#endif
        writer->SetFileType(2);
        std::string fileName;
        if (rootDir != "")
          {
          fileName = rootDir + std::string("/") + labelName + std::string("-Decimated.vtk");
          }
        else
          {
          fileName = labelName + std::string("-MarchingCubes.vtk");
          }
        if (debug)
          {
          watchWriter.QuietOn();
          std::cout << "Writing intermediate file " << fileName.c_str() << std::endl;
          }
        writer->SetFileName(fileName.c_str());
        if (!writer->Write())
          {
          std::cerr << "ERROR: Failed to write intermediate file " << fileName.c_str() << std::endl;
          }
#if (VTK_MAJOR_VERSION <= 5)
        writer->SetInput(NULL);
#else
        writer->SetInputData(NULL);
#endif
        writer = NULL;
        }
      if (transformIJKtoRAS == NULL ||
          transformIJKtoRAS->GetMatrix() == NULL)
        {
        std::cout << "transformIJKtoRAS is "
                  << (transformIJKtoRAS ==
            NULL ? "null" : "okay") << ", it's matrix is "
                  << (transformIJKtoRAS->GetMatrix() == NULL ? "null" : "okay") << endl;
        }
      else if ((transformIJKtoRAS->GetMatrix())->Determinant() < 0)
        {
        if (debug)
          {
          std::cout << "Determinant " << (transformIJKtoRAS->GetMatrix())->Determinant()
                    << " is less than zero, reversing..." << endl;
          }
        if (reverser)
          {
#if (VTK_MAJOR_VERSION <= 5)
          reverser->SetInput(NULL);
#else
          reverser->SetInputData(NULL);
#endif
          reverser = NULL;
          }
        reverser = vtkSmartPointer<vtkReverseSense>::New();
        std::string            comment7 = "Reverse " + labelName;
        vtkPluginFilterWatcher watchReverser(reverser,
                                             comment7.c_str(),
                                             CLPProcessInformation,
                                             1.0 / numFilterSteps,
                                             currentFilterOffset / numFilterSteps);
        currentFilterOffset += 1.0;
        if (debug)
          {
          watchReverser.QuietOn();
          }
#if (VTK_MAJOR_VERSION <= 5)
        reverser->SetInput(decimator->GetOutput());
#else
        reverser->SetInputConnection(decimator->GetOutputPort());
#endif
        reverser->ReverseNormalsOn();
#if (VTK_MAJOR_VERSION <= 5)
        (reverser->GetOutput())->ReleaseDataFlagOn();
#else
        reverser->ReleaseDataFlagOn();
#endif
        }

      if (JointSmoothing == 0)
        {
        if (strcmp(FilterType.c_str(), "Sinc") == 0)
          {

          if (smootherSinc)
            {
#if (VTK_MAJOR_VERSION <= 5)
            smootherSinc->SetInput(NULL);
#else
            smootherSinc->SetInputData(NULL);
#endif
            smootherSinc = NULL;
            }
          smootherSinc = vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
          std::string            comment8 = "Smooth " + labelName;
          vtkPluginFilterWatcher watchSmoother(smootherSinc,
                                               comment8.c_str(),
                                               CLPProcessInformation,
                                               1.0 / numFilterSteps,
                                               currentFilterOffset / numFilterSteps);
          currentFilterOffset += 1.0;
          if (debug)
            {
            watchSmoother.QuietOn();
            }
          smootherSinc->SetPassBand(0.1);
          if (Smooth == 1)
            {
            std::cerr << "Warning: Smoothing iterations of 1 not allowed for Sinc filter, using 2" << endl;
            Smooth = 2;
            }
          if ((transformIJKtoRAS->GetMatrix())->Determinant() < 0)
            {
#if (VTK_MAJOR_VERSION <= 5)
            smootherSinc->SetInput(reverser->GetOutput());
#else
            smootherSinc->SetInputConnection(reverser->GetOutputPort());
#endif
            }
          else
            {
#if (VTK_MAJOR_VERSION <= 5)
            smootherSinc->SetInput(decimator->GetOutput());
#else
            smootherSinc->SetInputConnection(decimator->GetOutputPort());
#endif
            }
          smootherSinc->SetNumberOfIterations(Smooth);
          smootherSinc->FeatureEdgeSmoothingOff();
          smootherSinc->BoundarySmoothingOff();
#if (VTK_MAJOR_VERSION <= 5)
          (smootherSinc->GetOutput())->ReleaseDataFlagOn();
          // smootherSinc->ReleaseDataFlagOn();
#else
          smootherSinc->ReleaseDataFlagOn();
#endif
          try
            {
            smootherSinc->Update();
            }
          catch(...)
            {
            std::cerr << "ERROR updating Sinc smoother for model " << i << std::endl;
            return EXIT_FAILURE;
            }
          }
        else
          {
          if (smootherPoly)
            {
#if (VTK_MAJOR_VERSION <= 5)
            smootherPoly->SetInput(NULL);
#else
            smootherPoly->SetInputData(NULL);
#endif
            smootherPoly = NULL;
            }
          smootherPoly = vtkSmartPointer<vtkSmoothPolyDataFilter>::New();
          std::string            comment9 = "Smooth " + labelName;
          vtkPluginFilterWatcher watchSmoother(smootherPoly,
                                               comment9.c_str(),
                                               CLPProcessInformation,
                                               1.0 / numFilterSteps,
                                               currentFilterOffset / numFilterSteps);
          currentFilterOffset += 1.0;
          if (debug)
            {
            watchSmoother.QuietOn();
            }

          // this next line massively rounds corners
          smootherPoly->SetRelaxationFactor(0.33);
          smootherPoly->SetFeatureAngle(60);
          smootherPoly->SetConvergence(0);

          if ((transformIJKtoRAS->GetMatrix())->Determinant() < 0)
            {
#if (VTK_MAJOR_VERSION <= 5)
            smootherPoly->SetInput(reverser->GetOutput());
#else
            smootherPoly->SetInputConnection(reverser->GetOutputPort());
#endif
            }
          else
            {
#if (VTK_MAJOR_VERSION <= 5)
            smootherPoly->SetInput(decimator->GetOutput());
#else
            smootherPoly->SetInputConnection(decimator->GetOutputPort());
#endif
            }
          smootherPoly->SetNumberOfIterations(Smooth);
          smootherPoly->FeatureEdgeSmoothingOff();
          smootherPoly->BoundarySmoothingOff();
#if (VTK_MAJOR_VERSION <= 5)
          (smootherPoly->GetOutput())->ReleaseDataFlagOn();
          // smootherPoly->ReleaseDataFlagOn();
#else
          smootherPoly->ReleaseDataFlagOn();
#endif
          try
            {
            smootherPoly->Update();
            }
          catch(...)
            {
            std::cerr << "ERROR updating Poly smoother for model " << i << std::endl;
            return EXIT_FAILURE;
            }
          }

        if (SaveIntermediateModels)
          {
          writer = vtkSmartPointer<vtkPolyDataWriter>::New();
          std::string            commentSaveSmoothed = "Writing intermediate model after smoothing " + labelName;
          vtkPluginFilterWatcher watchWriter(writer,
                                             commentSaveSmoothed.c_str(),
                                             CLPProcessInformation,
                                             1.0 / numFilterSteps,
                                             currentFilterOffset / numFilterSteps);
          currentFilterOffset += 1.0;
          if (strcmp(FilterType.c_str(), "Sinc") == 0)
            {
#if (VTK_MAJOR_VERSION <= 5)
            writer->SetInput(smootherSinc->GetOutput());
#else
            writer->SetInputConnection(smootherSinc->GetOutputPort());
#endif
            }
          else
            {
#if (VTK_MAJOR_VERSION <= 5)
           writer->SetInput(smootherPoly->GetOutput());
#else
            writer->SetInputConnection(smootherPoly->GetOutputPort());
#endif
            }
          writer->SetFileType(2);
          std::string fileName;
          if (rootDir != "")
            {
            fileName = rootDir + std::string("/") + labelName + std::string("-Smoothed.vtk");
            }
          else
            {
            fileName = labelName + std::string("-Smoothed.vtk");
            }
          if (debug)
            {
            watchWriter.QuietOn();
            std::cout << "Writing intermediate file " << fileName.c_str() << std::endl;
            }
          writer->SetFileName(fileName.c_str());
          if (!writer->Write())
            {
            std::cerr << "ERROR: Failed to write intermediate file " << fileName.c_str() << std::endl;
            }
#if (VTK_MAJOR_VERSION <= 5)
          writer->SetInput(NULL);
#else
          writer->SetInputData(NULL);
#endif
          writer = NULL;
          }
        }

      if (transformer)
        {
#if (VTK_MAJOR_VERSION <= 5)
        transformer->SetInput(NULL);
#else
        transformer->SetInputData(NULL);
#endif
        transformer = NULL;
        }
      transformer = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
      std::string            comment1 = "Transform " + labelName;
      vtkPluginFilterWatcher watchTransformer(transformer,
                                              comment1.c_str(),
                                              CLPProcessInformation,
                                              1.0 / numFilterSteps,
                                              currentFilterOffset / numFilterSteps);
      currentFilterOffset += 1.0;
      if (debug)
        {
        watchTransformer.QuietOn();
        }
      if (JointSmoothing == 0)
        {
        if (strcmp(FilterType.c_str(), "Sinc") == 0)
          {
#if (VTK_MAJOR_VERSION <= 5)
          transformer->SetInput(smootherSinc->GetOutput());
#else
          transformer->SetInputConnection(smootherSinc->GetOutputPort());
#endif
          }
        else
          {
#if (VTK_MAJOR_VERSION <= 5)
          transformer->SetInput(smootherPoly->GetOutput());
#else
          transformer->SetInputConnection(smootherPoly->GetOutputPort());
#endif
          }
        }
      else
        {
        if ((transformIJKtoRAS->GetMatrix())->Determinant() < 0)
          {
#if (VTK_MAJOR_VERSION <= 5)
          transformer->SetInput(reverser->GetOutput());
#else
          transformer->SetInputConnection(reverser->GetOutputPort());
#endif
          }
        else
          {
#if (VTK_MAJOR_VERSION <= 5)
          transformer->SetInput(decimator->GetOutput());
#else
          transformer->SetInputConnection(decimator->GetOutputPort());
#endif
          }
        }

      transformer->SetTransform(transformIJKtoRAS);
      if (debug)
        {
        // transformIJKtoRAS->GetMatrix()->Print(std::cout);
        }

#if (VTK_MAJOR_VERSION <= 5)
      (transformer->GetOutput())->ReleaseDataFlagOn();
#else
      transformer->ReleaseDataFlagOn();
#endif
      if (normals)
        {
#if (VTK_MAJOR_VERSION <= 5)
        normals->SetInput(NULL);
#else
        normals->SetInputData(NULL);
#endif
        normals = NULL;
        }
      normals = vtkSmartPointer<vtkPolyDataNormals>::New();
      std::string            comment2 = "Normals " + labelName;
      vtkPluginFilterWatcher watchNormals(normals,
                                          comment2.c_str(),
                                          CLPProcessInformation,
                                          1.0 / numFilterSteps,
                                          currentFilterOffset / numFilterSteps);
      currentFilterOffset += 1.0;
      if (debug)
        {
        watchNormals.QuietOn();
        }

      if (PointNormals)
        {
        normals->ComputePointNormalsOn();
        }
      else
        {
        normals->ComputePointNormalsOff();
        }
#if (VTK_MAJOR_VERSION <= 5)
      normals->SetInput(transformer->GetOutput());
#else
      normals->SetInputConnection(transformer->GetOutputPort());
#endif
      normals->SetFeatureAngle(60);
      normals->SetSplitting(SplitNormals);

#if (VTK_MAJOR_VERSION <= 5)
      (normals->GetOutput())->ReleaseDataFlagOn();
#else
      normals->ReleaseDataFlagOn();
#endif

      if (stripper)
        {
#if (VTK_MAJOR_VERSION <= 5)
        stripper->SetInput(NULL);
#else
        stripper->SetInputData(NULL);
#endif
        stripper = NULL;
        }
      stripper = vtkSmartPointer<vtkStripper>::New();
      std::string            comment3 = "Strip " + labelName;
      vtkPluginFilterWatcher watchStripper(stripper,
                                           comment3.c_str(),
                                           CLPProcessInformation,
                                           1.0 / numFilterSteps,
                                           currentFilterOffset / numFilterSteps);
      currentFilterOffset += 1.0;
      if (debug)
        {
        watchStripper.QuietOn();
        }
#if (VTK_MAJOR_VERSION <= 5)
      stripper->SetInput(normals->GetOutput());
      (stripper->GetOutput())->ReleaseDataFlagOff();

#else
      stripper->SetInputConnection(normals->GetOutputPort());
      stripper->ReleaseDataFlagOff();
#endif

      // the poly data output from the stripper can be set as an input to a
      // model's polydata
      try
        {
#if (VTK_MAJOR_VERSION <= 5)
        (stripper->GetOutput())->Update();
#else
        stripper->Update();
#endif
        }
      catch(...)
        {
        std::cerr << "ERROR updating stripper for model " << i << std::endl;
        return EXIT_FAILURE;
        }

      // but for now we're just going to write it out
      writer = vtkSmartPointer<vtkPolyDataWriter>::New();
      std::string            comment4 = "Write " + labelName;
      vtkPluginFilterWatcher watchWriter(writer,
                                         comment4.c_str(),
                                         CLPProcessInformation,
                                         1.0 / numFilterSteps,
                                         currentFilterOffset / numFilterSteps);
      currentFilterOffset += 1.0;
      if (debug)
        {
        watchWriter.QuietOn();
        }
#if (VTK_MAJOR_VERSION <= 5)
      writer->SetInput(stripper->GetOutput());
#else
      writer->SetInputConnection(stripper->GetOutputPort());
#endif
      writer->SetFileType(2);
      std::string fileName;
      if (rootDir != "")
        {
        fileName = rootDir + std::string("/") + labelName + std::string(".vtk");
        }
      else
        {
        std::cout << "WARNING: output directory is an empty string..." << endl;
        fileName = labelName + std::string(".vtk");
        }
      writer->SetFileName(fileName.c_str());

      if (debug)
        {
        std::cout << "Writing model " << " " << labelName << " to file " << writer->GetFileName()  << endl;
        }
      if (!writer->Write())
        {
        std::cerr << "ERROR: Failed to write model file " << fileName.c_str() << std::endl;
        }
#if (VTK_MAJOR_VERSION <= 5)
      writer->SetInput(NULL);
#else
      writer->SetInputData(NULL);
#endif
      writer = NULL;
      if (modelScene.GetPointer() != NULL)
        {
        if (debug)
          {
          std::cout << "Adding model " << labelName << " to the output scene, with filename " << fileName.c_str()
                    << endl;
          }
        // each model needs a mrml node, a storage node and a display node
        vtkNew<vtkMRMLModelNode> mnode;
        mnode->SetScene(modelScene.GetPointer());
        mnode->SetName(labelName.c_str());

        vtkNew<vtkMRMLModelStorageNode> snode;
        snode->SetFileName(fileName.c_str());
        if (modelScene->AddNode(snode.GetPointer()) == NULL)
          {
          std::cerr << "ERROR: unable to add the storage node to the model scene" << endl;
          }
        vtkNew<vtkMRMLModelDisplayNode> dnode;
        dnode->SetColor(0.5, 0.5, 0.5);
        double *rgba;
        if (colorNode != NULL)
          {
          rgba = colorNode->GetLookupTable()->GetTableValue(i);
          if (rgba != NULL)
            {
            if (debug)
              {
              std::cout << "Got colour: " << rgba[0] << " " << rgba[1] << " " << rgba[2] << " " << rgba[3] << endl;
              }
            dnode->SetColor(rgba[0], rgba[1], rgba[2]);
            }
          else
            {
            std::cerr << "Couldn't get look up table value for " << i << ", display node colour is not set (grey)"
                      << endl;
            }
          }

        dnode->SetVisibility(1);
        modelScene->AddNode(dnode.GetPointer());
        if (debug)
          {
          std::cout << "Added display node: id = " << (dnode->GetID() == NULL ? "(null)" : dnode->GetID()) << endl;
          std::cout << "Setting model's storage node: id = "
                    << (snode->GetID() == NULL ? "(null)" : snode->GetID()) << endl;
          }
        mnode->SetAndObserveStorageNodeID(snode->GetID());
        mnode->SetAndObserveDisplayNodeID(dnode->GetID());
        modelScene->AddNode(mnode.GetPointer());

        // put it in the hierarchy, either the flat one by default or
        // try to find the matching color hierarchy node to make this an
        // associated node
        std::string colorName;
        if (colorNode != NULL)
          {
          colorName = std::string(colorNode->GetColorNameAsFileName(i));
          }
        else
          {
          // might be in a testing case where the hierarchy nodes are
          // numbered (made from the generic colors)
          std::stringstream ss;
          ss << i;
          colorName = ss.str();
          if (debug)
            {
            std::cout << "No color node, guessing at color name being same as label number " << colorName.c_str() << std::endl;
            }
          }
        vtkMRMLNode *mrmlNode = NULL;
        if (colorName.compare("") != 0)
          {
          mrmlNode = modelScene->GetFirstNodeByName(colorName.c_str());
          }
        // if there's no color hierarchy, or no color name or the mrml node
        // named for the color isn't a model hierarchy node, use a flat hierarchy
        if (topColorHierarchyNode == NULL ||
            colorName.compare("") == 0 ||
            mrmlNode == NULL ||
            strcmp(mrmlNode->GetClassName(),"vtkMRMLModelHierarchyNode") != 0)
          {
          vtkNew<vtkMRMLModelHierarchyNode> mhnd;
          mhnd->SetHideFromEditors(1);
          modelScene->AddNode(mhnd.GetPointer());
          mhnd->SetParentNodeID(rnd->GetID());
          mhnd->SetModelNodeID(mnode->GetID());
          }
        else
          {
          // use the template color hierarchy
          vtkMRMLModelHierarchyNode *colorHierarchyNode = vtkMRMLModelHierarchyNode::SafeDownCast(mrmlNode);
          if (colorHierarchyNode)
            {
            colorHierarchyNode->SetAssociatedNodeID(mnode->GetID());
            // and hide it so that it doesn't clutter up the tree
            colorHierarchyNode->SetHideFromEditors(1);
            if (debug)
              {
              std::cout << "Found a color hierarchy node with name " << colorHierarchyNode->GetName() << ", set it's associated node to this model id: " << mnode->GetID() << std::endl;
              }
            }
          }
        if (debug)
          {
          std::cout << "...done adding model to output scene" << endl;
          }
        }
      } // end of skipping an empty label
    }   // end of loop over labels
  if (debug)
    {
    std::cout << "End of looping over labels" << endl;
    }
  // Report what was done
  if (madeModels.size() > 0)
    {
    std::cout << "Made models from labels:";
    for(::size_t i = 0; i < madeModels.size(); i++)
      {
      std::cout << " " << madeModels[i];
      }
    std::cout << endl;
    }
  if (skippedModels.size() > 0)
    {
    std::cout << "Skipped making models from labels:";
    for(::size_t i = 0; i < skippedModels.size(); i++)
      {
      std::cout << " " << skippedModels[i];
      }
    std::cout << endl;
    }
  if (sceneFilename != "")
    {
    if (debug)
      {
      std::cout << "Writing to model scene output file: " << sceneFilename.c_str();
      std::cout << ", to url: " << modelScene->GetURL() << std::endl;
      }
    // take out the colour nodes first
    if (colorStorageNode != NULL)
      {
      modelScene->RemoveNode(colorStorageNode);
      }
    if (colorNode != NULL)
      {
      modelScene->RemoveNode(colorNode);
      }
    // take out any extra hierarchy nodes and display nodes
    if (topColorHierarchyNode != NULL)
      {
      // get all the hierarchies under it, recursively
      std::vector< vtkMRMLHierarchyNode* > allChildren;
      topColorHierarchyNode->GetAllChildrenNodes(allChildren);
      for (unsigned int i = 0; i < allChildren.size(); i++)
        {
        if (allChildren[i]->GetAssociatedNodeID() == NULL &&
            allChildren[i]->GetNumberOfChildrenNodes() == 0)
          {
          // if this child doesn't have an associated node, nor does it have children nodes (keep the structure of the hierarchy), remove it and it's display node
          if (debug)
            {
            std::cout << "Removing extraneous hierarchy node " << allChildren[i]->GetName() << std::endl;
            }
          if (vtkMRMLDisplayableHierarchyNode::SafeDownCast(allChildren[i]) != NULL)
            {
            vtkMRMLDisplayNode *hierarchyDisplayNode = vtkMRMLDisplayableHierarchyNode::SafeDownCast(allChildren[i])->GetDisplayNode();
            if (hierarchyDisplayNode)
              {
              if (debug)
                {
                std::cout << "\tand disp node " << hierarchyDisplayNode->GetID() << std::endl;
                }
              modelScene->RemoveNode(hierarchyDisplayNode);
              }
            }
          modelScene->RemoveNode(allChildren[i]);
          }
        }
      }
    // write to disk
    modelScene->Commit();
    std::cout << "Models saved to scene file " << sceneFilename.c_str() << "\n";
    if (ModelSceneFile.size() == 0)
      {
      std::cout << "\nIf you ran this from Slicer3's GUI, use File->Import Scene... " << sceneFilename.c_str()
                << " to load your models.\n";
      }
    }

  // Clean up
  if (debug)
    {
    std::cout << "Cleaning up" << endl;
    }
  if (cubes)
    {
    if (debug)
      {
      std::cout << "Deleting cubes" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    cubes->SetInput(NULL);
#else
    cubes->SetInputData(NULL);
#endif
    cubes = NULL;
    }
  if (colorNode)
    {
    colorNode = NULL;
    }
  if (smoother)
    {
    if (debug)
      {
      std::cout << "Deleting smoother" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    smoother->SetInput(NULL);
#else
    smoother->SetInputData(NULL);
#endif
    smoother = NULL;
    }
  if (hist)
    {
    if (debug)
      {
      std::cout << "Deleting hist" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    hist->SetInput(NULL);
#else
    hist->SetInputData(NULL);
#endif
    hist = NULL;
    }
  if (smootherSinc)
    {
    if (debug)
      {
      std::cout << "Deleting smootherSinc" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    smootherSinc->SetInput(NULL);
#else
    smootherSinc->SetInputData(NULL);
#endif
    smootherSinc = NULL;
    }
  if (smootherPoly)
    {
    if (debug)
      {
      std::cout << "Deleting smoother poly" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    smootherPoly->SetInput(NULL);
#else
    smootherPoly->SetInputData(NULL);
#endif
    smootherPoly = NULL;
    }
  if (decimator)
    {
    if (debug)
      {
      std::cout << "Deleting decimator" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    decimator->SetInput(NULL);
#else
    decimator->SetInputData(NULL);
#endif
    decimator = NULL;
    }
  if (mcubes)
    {
    if (debug)
      {
      std::cout << "Deleting mcubes" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    mcubes->SetInput(NULL);
#else
    mcubes->SetInputData(NULL);
#endif
    mcubes = NULL;
    }
  if (imageThreshold)
    {
    if (debug)
      {
      std::cout << "Deleting image threshold" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    imageThreshold->SetInput(NULL);
#else
    imageThreshold->SetInputData(NULL);
#endif
    imageThreshold->RemoveAllInputs();
    imageThreshold = NULL;
    if (debug)
      {
      std::cout << "... done deleting image threshold" << endl;
      }
    }
  if (threshold)
    {
    if (debug)
      {
      cout << "Deleting threshold" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    threshold->SetInput(NULL);
#else
    threshold->SetInputData(NULL);
#endif
    threshold = NULL;
    }
  if (imageToStructuredPoints)
    {
    if (debug)
      {
      std::cout << "Deleting image to structured points" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    imageToStructuredPoints->SetInput(NULL);
#else
    imageToStructuredPoints->SetInputData(NULL);
#endif
    imageToStructuredPoints = NULL;
    }
  if (geometryFilter)
    {
    if (debug)
      {
      cout << "Deleting geometry filter" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    geometryFilter->SetInput(NULL);
#else
    geometryFilter->SetInputData(NULL);
#endif
    geometryFilter = NULL;
    }
  if (transformIJKtoRAS)
    {
    if (debug)
      {
      std::cout << "Deleting transform ijk to ras" << endl;
      }
    transformIJKtoRAS->SetInput(NULL);
    transformIJKtoRAS = NULL;
    }
  if (reverser)
    {
    if (debug)
      {
      std::cout << "Deleting reverser" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    reverser->SetInput(NULL);
#else
    reverser->SetInputData(NULL);
#endif
    reverser = NULL;
    }
  if (transformer)
    {
    if (debug)
      {
      std::cout << "Deleting transformer" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    transformer->SetInput(NULL);
#else
    transformer->SetInputData(NULL);
#endif
    transformer = NULL;
    }
  if (normals)
    {
    if (debug)
      {
      std::cout << "Deleting normals" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    normals->SetInput(NULL);
#else
    normals->SetInputData(NULL);
#endif
    normals = NULL;
    }
  if (stripper)
    {
    if (debug)
      {
      std::cout << "Deleting stripper" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    stripper->SetInput(NULL);
#else
    stripper->SetInputData(NULL);
#endif
    stripper = NULL;
    }
  if (ici.GetPointer())
    {
    if (debug)
      {
      std::cout << "Deleting ici, no set input null" << endl;
      }
#if (VTK_MAJOR_VERSION <= 5)
    ici->SetInput(NULL);
#else
    ici->SetInputData(NULL);
#endif
    }
  if (debug)
    {
    std::cout << "Deleting reader" << endl;
    }
  reader = NULL;

  if (modelScene.GetPointer())
    {
    if (debug)
      {
      std::cout << "Deleting model scene" << endl;
      }
    modelScene->Clear(1);
    }
  return EXIT_SUCCESS;
}
