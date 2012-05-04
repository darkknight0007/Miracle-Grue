

#include "JsonConverter.h"
#include <json/writer.h>

// #include "abstractable.h"
#include "miracle.h"

using namespace std;
using namespace mgl;
using namespace Json;
using namespace libthing;



//// @param slices list of output slice (output )
void mgl::miracleGrue(GCoder &gcoderCfg,
                      const SlicerConfig &slicerCfg,
                      const char *modelFileStr,
                      const char *scadFileStr,
                      const char *gcodeFileStr,
                      int firstSliceIdx,
                      int lastSliceIdx,
                      ModelSkeleton &skeleton,
                      std::vector< SliceData >  &slices,
                      ProgressBar *progress)
{


	unsigned int roofLayerCount = 3;
	unsigned int floorLayerCount = 3;
	unsigned int skipCount = 1;

	Skeletor skeletor;
	skeletor.init(slicerCfg, 0.95,
					roofLayerCount,
					floorLayerCount,
					skipCount,
					progress);

	skeletor.outlines(modelFileStr,
					skeleton.layerMeasure,
					skeleton.grid,
					skeleton.outlines);

	skeletor.insets(skeleton.outlines,
				  skeleton.insets);

	skeletor.flatSurfaces(skeleton.insets,
						skeleton.grid,
						skeleton.flatSurfaces);

	skeletor.roofing(skeleton.flatSurfaces, skeleton.grid, skeleton.roofings);

	skeletor.infills( skeleton.flatSurfaces,
					skeleton.grid,
					skeleton.roofings,
					skeleton.floorings,
					skeleton.infills);

	Slicor slicor;
	slicor.init(gcoderCfg, progress);

	size_t sliceCount = skeleton.outlines.size();


    if(firstSliceIdx == -1) firstSliceIdx = 0;
    if(lastSliceIdx  == -1) lastSliceIdx = sliceCount-1;

	slices.resize(sliceCount);
	bool direction = false;
	unsigned int currentSlice = 0;

	if(progress){progress->reset(sliceCount, "Path generation");}

	for(size_t i=0; i < sliceCount; i++)
	{
		if(progress){progress->tick();}

        if(i <  firstSliceIdx) continue;
        if(i > lastSliceIdx) break;

		direction = !direction;
		SliceData& slice = slices[i];

		Scalar z = skeleton.layerMeasure.sliceIndexToHeight(currentSlice);
		currentSlice ++;

		slice.updatePosition(z, i);
		slice.extruderSlices.resize(1);

		ExtruderSlice &extruderSlice = slice.extruderSlices[0];

		const Insets &insets = skeleton.insets[i];
		const SegmentTable &outlineSegments = skeleton.outlines[i];

		slicor.outlines(outlineSegments, extruderSlice.boundary);
		slicor.insets(insets, extruderSlice.insetLoopsList);

		const GridRanges &infillRanges = skeleton.infills[i];

		Polygons &infills = extruderSlice.infills;
		slicor.infills(infillRanges, skeleton.grid, direction, infills);
	}


	slicor.writeGcode(gcodeFileStr, modelFileStr, slices);

}


 
///
/// Creates slices from the specified model file, and saves them
/// into the slices object
///
/// @param slices - List of slices to write
/// @param zIndicides - list of zHeight of each slice in slices, indexed by position
/// @param slicer - instance of a slicer object
/// @param firstSliceIdx - for debugging, first slice to store into slices.
/// @param lastSliceIdx - for debugging, last slice to store into slices.
/// @param modelSource - source .stl filename
/// @param scadFile - debugging SCAD filename for debugging to scad file
void mgl::slicesFromSlicerAndMesh(
		std::vector< SliceData >  &slices,
		const SlicerConfig &slicer,
		Meshy& mesh,
		const char *scadFile,
		int firstSliceIdx,
                int lastSliceIdx,
                ProgressBar *progressPtr)
{
    assert(slices.size() ==0);

    unsigned int sliceCount = mesh.readSliceTable().size();
    unsigned int extruderId = 0;

    if(firstSliceIdx == -1) firstSliceIdx = 0;
    if(lastSliceIdx  == -1) lastSliceIdx = sliceCount-1;

    Slicy slicy(mesh.readAllTriangles(), mesh.readLimits(),
                slicer.layerW, slicer.layerH, sliceCount, scadFile);

    /// for future use of multithreading, we need to create slices to not lock
    /// the structure in the loop
    slices.reserve( mesh.readSliceTable().size());
    for(unsigned int sliceId=0; sliceId < sliceCount; sliceId++)
    {
        Scalar z = mesh.readLayerMeasure().sliceIndexToHeight(sliceId);
        slices.push_back( SliceData(z,sliceId) );
    }

    Scalar cuttOffLength = slicer.insetCuttOffMultiplier * slicer.layerW;

    ProgressLog progressSlice;
    progressPtr = progressPtr?progressPtr:&progressSlice;
    ProgressBar &progress = *progressPtr;

    progress.reset(sliceCount, "Slicing");
    Log::often() << "Slicing" << endl;

    for(unsigned int sliceId=0; sliceId < sliceCount; sliceId++)
    {
            progress.tick();
            if(sliceId <  firstSliceIdx) continue;
            if(sliceId > lastSliceIdx) break;

            const TriangleIndices & trianglesForSlice = mesh.readSliceTable()[sliceId];
            Scalar sliceAngle = sliceId * slicer.angle;
            SliceData &slice = slices[sliceId];

            slicy.slice(	trianglesForSlice,
                                            sliceId,
                                            extruderId,
                                            slicer.tubeSpacing,
                                            sliceAngle,
                                            slicer.nbOfShells,
                                            cuttOffLength,
                                            slicer.infillShrinkingMultiplier,
                                            slicer.insetDistanceMultiplier,
                                            slicer.writeDebugScadFiles,
                                            slice);
    }
}


///
/// Does an inplcae update to the passed slices to adjust/move them
/// so the lowest layer is a z height zero
/// @param slices slices to adjust
/// @param layerMeasure class for layer measurements in Z plane
/// @param firstSliceIdx - 'bottom' of slice section to transport down. -1 indicates auto-detect
/// @param lastSliceIdx - 'top' of slice section to transport down . -1 indicates auto-detect
///
void mgl::adjustSlicesToPlate(
		std::vector<SliceData>& slices,
		const LayerMeasure& layerMeasure,
                int firstSliceIdx,
                int lastSliceIdx)
{
	assert(slices.size() > 0);

	size_t sliceCounter = 0;
	size_t first = 0, last = 0;

	if(firstSliceIdx == -1  ) {
		first  = slices.size();
	}

	else if(firstSliceIdx > 0 ) {
		first  = firstSliceIdx;
	}

	if(lastSliceIdx == -1 || lastSliceIdx <= slices.size() ){
		last = slices.size()-1;
	}
	else{
		last = lastSliceIdx;
	}
    Log::often() << "range start: " << first << endl;
    Log::often() << "range end : " << last << endl;



	//adjust slices we are keeping in-place
	for(size_t sliceId = firstSliceIdx; sliceId <= lastSliceIdx; sliceId++, sliceCounter++)
	{
		Scalar adjustedZ = layerMeasure.sliceIndexToHeight(sliceCounter);
		slices[sliceId].updatePosition(adjustedZ, sliceCounter);
	}


	//remove 'end' items from slices
	slices.erase(slices.begin() + last+1, slices.end());

	//remove 'front' items from slices
	slices.erase(slices.begin(), slices.begin() + first);
}



 /// Writes to gcodeFile via gcoder the slices and data specified by slices
 /// @param gcodeFile - name out output gcode file
 /// @param gcoder  - instance of a gcode object
 /// @param slices - List of slices to write
 /// @param modelSource - source of model data, usually the source .stl filename
void mgl::writeGcodeFromSlicesAndParams(
		const char *gcodeFile,
		GCoder &gcoder,
		std::vector<SliceData >& slices,
                const char *modelSource, // = "N/A"
                ProgressBar *progressPtr  ) // = NULL
{

    assert(slices.size() != 0);
    assert(gcodeFile != 0x00);
    assert(modelSource != 0x00);
    size_t sliceCount = slices.size();

    Log::often() << "Writing gcode" << endl;
    ProgressLog progressGcode(sliceCount);
    progressPtr = progressPtr?progressPtr:&progressGcode;
    ProgressBar &progress = *progressPtr;

    std::ofstream gout(gcodeFile);
    gcoder.writeStartOfFile(gout, modelSource);
    progress.reset(sliceCount, "Gcoding");
    for(size_t sliceId=0; sliceId < sliceCount; sliceId++)
    {
        progress.tick();
        SliceData &slice = slices[sliceId];
        gcoder.writeSlice(gout, slice);
    }
    gout.close();
}

/// Logs a stack of slices to a logging directory for debugging
/// creates the log directory if needed.
///
void mgl::slicesLogToDir(std::vector<SliceData>& slices, const char* logDirName)
{
	FileSystemAbstractor fs;
	JsonConverter converter;
	StyledStreamWriter streamWriter;

	/// check if the dir exists, create if needed
    fs.guarenteeDirectoryExists(logDirName );

    Log::often() << "logging slices to dir" << endl;

	///for each slice, dump to a json file of 'slice_idx_NUM'
	for(size_t i = 0; i < slices.size(); i++){
        Log::often() << "Writing slice " << endl;
		SliceData& d = slices[i];
		Value val;
		stringstream ss;
		converter.loadJsonFromSliceData(val, d);
		ss << "slice_" << i << ".json";
		string outFile = fs.pathJoin(string(logDirName), ss.str());
		ofstream dumper(outFile.c_str());
		streamWriter.write(dumper,val);
		dumper.close();
	}
}

