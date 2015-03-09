/** Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License */
#include "infill.h"

namespace cura {

void generateConcentricInfill(Polygons outline, Polygons& result, int inset_value)
{
    while(outline.size() > 0)
    {
        for (unsigned int polyNr = 0; polyNr < outline.size(); polyNr++)
        {
            PolygonRef r = outline[polyNr];
            result.add(r);
        }
        outline = outline.offset(-inset_value);
    }
}

void generateGridInfill(const Polygons& in_outline, Polygons& result,
                        int extrusionWidth, int lineSpacing, int infillOverlap,
                        double rotation)
{
    generateLineInfill(in_outline, result, extrusionWidth, lineSpacing * 2,
                       infillOverlap, rotation);
    generateLineInfill(in_outline, result, extrusionWidth, lineSpacing * 2,
                       infillOverlap, rotation + 90);
}



/*!
 * generate lines within the area of [in_outline], at regular intervals of [lineSpacing]
 * idea:
 * intersect a regular grid of 'scanlines' with the area inside [in_outline]
 * 
 * we call the areas between two consecutive scanlines a 'scansegment'.
 * Scansegment x is the area between scanline x and scanline x+1
 * 
 * algorithm:
 * 1) for each line segment of each polygon:
 *      store the intersections of that line segment with all scanlines in a mapping (vector of vectors) from scanline to intersections
 *      (zigzag): add boundary segments to result
 * 2) for each scanline:
 *      sort the associated intersections 
 *      and connect them using the even-odd rule
 * 
 */
void generateLineInfill(const Polygons& in_outline, Polygons& result, int extrusionWidth, int lineSpacing, int infillOverlap, double rotation)
{
    if (in_outline.size() == 0) return;
    Polygons outline = in_outline.offset(extrusionWidth * infillOverlap / 100);
    if (outline.size() == 0) return;
    
    PointMatrix matrix(rotation);
    
    outline.applyMatrix(matrix);
    
    auto addLine = [&](Point from, Point to)
    {            
        PolygonRef p = result.newPoly();
        p.add(matrix.unapply(from));
        p.add(matrix.unapply(to));
    };
    auto compare_int64_t = [](const void* a, const void* b)
    {
        int64_t n = (*(int64_t*)a) - (*(int64_t*)b);
        if (n < 0) return -1;
        if (n > 0) return 1;
        return 0;
    };
    
    
    AABB boundary(outline);
    
    int scanline_min_idx = boundary.min.X / lineSpacing;
    int lineCount = (boundary.max.X + (lineSpacing - 1)) / lineSpacing - scanline_min_idx;
  
    std::vector<std::vector<int64_t> > cutList; // mapping from scanline to all intersections with polygon segments
    
    for(int n=0; n<lineCount; n++)
        cutList.push_back(std::vector<int64_t>());
    
    for(unsigned int polyNr=0; polyNr < outline.size(); polyNr++)
    {
        Point p0 = outline[polyNr][outline[polyNr].size()-1];
        for(unsigned int i=0; i < outline[polyNr].size(); i++)
        {
            Point p1 = outline[polyNr][i];
            int64_t xMin = p1.X, xMax = p0.X;
            if (xMin == xMax) continue; 
            if (xMin > xMax) { xMin = p0.X; xMax = p1.X; }
            
            int scanline_idx0 = (p0.X + ((p0.X > 0)? -1 : -lineSpacing)) / lineSpacing; // -1 cause a linesegment on scanline x counts as belonging to scansegment x-1   ...
            int scanline_idx1 = (p1.X + ((p1.X > 0)? -1 : -lineSpacing)) / lineSpacing; // -linespacing because a line between scanline -n and -n-1 belongs to scansegment -n-1 (for n=positive natural number)
            int direction = 1;
            if (p0.X > p1.X) 
            { 
                direction = -1; 
                scanline_idx1 += 1; // only consider the scanlines in between the scansegments
            } else scanline_idx0 += 1; // only consider the scanlines in between the scansegments
            
            for(int scanline_idx = scanline_idx0; scanline_idx != scanline_idx1+direction; scanline_idx+=direction)
            {
                int x = scanline_idx * lineSpacing;
                int y = p1.Y + (p0.Y - p1.Y) * (x - p1.X) / (p0.X - p1.X);
                cutList[scanline_idx - scanline_min_idx].push_back(y);
            }
            p0 = p1;
        }
    }
    
    int scanline_idx = 0;
    for(int64_t x = scanline_min_idx * lineSpacing; x < boundary.max.X; x += lineSpacing)
    {
        qsort(cutList[scanline_idx].data(), cutList[scanline_idx].size(), sizeof(int64_t), compare_int64_t);
        for(unsigned int i = 0; i + 1 < cutList[scanline_idx].size(); i+=2)
        {
            if (cutList[scanline_idx][i+1] - cutList[scanline_idx][i] < extrusionWidth / 5)
                continue;
            addLine(Point(x, cutList[scanline_idx][i]), Point(x, cutList[scanline_idx][i+1]));
        }
        scanline_idx += 1;
    }
}


/*!
 * adapted from generateLineInfill(.)
 * 
 * generate lines within the area of [in_outline], at regular intervals of [lineSpacing]
 * idea:
 * intersect a regular grid of 'scanlines' with the area inside [in_outline]
 * sigzag:
 * include pieces of boundary, connecting the lines, forming an accordion like zigzag instead of separate lines    |_|^|_|
 * 
 * we call the areas between two consecutive scanlines a 'scansegment'
 * 
 * algorithm:
 * 1. for each line segment of each polygon:
 *      store the intersections of that line segment with all scanlines in a mapping (vector of vectors) from scanline to intersections
 *      (zigzag): add boundary segments to result
 * 2. for each scanline:
 *      sort the associated intersections 
 *      and connect them using the even-odd rule
 * 
 * zigzag algorithm:
 * while walking around (each) polygon (1.)
 *  if polygon intersects with even scanline
 *      start boundary segment (add each following segment to the [result])
 *  when polygon intersects with a scanline again
 *      stop boundary segment (stop adding segments to the [result])
 *      if polygon intersects with even scanline again (instead of odd)
 *          dont add the last line segment to the boundary (unless [connect_zigzags])
 * 
 * 
 *     <--
 *     ___
 *    |   |   |
 *    |   |   |
 *    |   |___|
 *         -->
 * 
 *        ^ = even scanline
 * 
 * start boundary from even scanline! :D
 * 
 */
void generateZigZagInfill(const Polygons& in_outline, Polygons& result, int extrusionWidth, int lineSpacing, int infillOverlap, double rotation, bool connect_zigzags)
{    
//     if (in_outline.size() == 0) return;
//     Polygons outline = in_outline.offset(extrusionWidth * infillOverlap / 100);
    Polygons outline = in_outline; // .offset(extrusionWidth * infillOverlap / 100);
    if (outline.size() == 0) return;
    
    PointMatrix matrix(rotation);
    
    outline.applyMatrix(matrix);
    
    auto addLine = [&](Point from, Point to)
    {            
        PolygonRef p = result.newPoly();
        p.add(matrix.unapply(from));
        p.add(matrix.unapply(to));
    };   
        
    AABB boundary(outline);
    
    int scanline_min_idx = boundary.min.X / lineSpacing;
    int lineCount = (boundary.max.X + (lineSpacing - 1)) / lineSpacing - scanline_min_idx;
    
    std::vector<std::vector<int64_t> > cutList; // mapping from scanline to all intersections with polygon segments
    
    for(int n=0; n<lineCount; n++)
        cutList.push_back(std::vector<int64_t>());
    for(unsigned int polyNr=0; polyNr < outline.size(); polyNr++)
    {
        std::vector<Point> firstBoundarySegment;
        std::vector<Point> unevenBoundarySegment; // stored cause for connected_zigzags a boundary segment which ends in an uneven scanline needs to be included
        
        bool isFirstBoundarySegment = true;
        bool firstBoundarySegmentEndsInEven;
        
        bool isEvenScanSegment = false; 
        
        
        Point p0 = outline[polyNr][outline[polyNr].size()-1];
        Point lastPoint = p0;
        for(unsigned int i=0; i < outline[polyNr].size(); i++)
        {
            Point p1 = outline[polyNr][i];
            int64_t xMin = p1.X, xMax = p0.X;
            if (xMin == xMax) continue; 
            if (xMin > xMax) { xMin = p0.X; xMax = p1.X; }
            
            int scanline_idx0 = (p0.X + ((p0.X > 0)? -1 : -lineSpacing)) / lineSpacing; // -1 cause a linesegment on scanline x counts as belonging to scansegment x-1   ...
            int scanline_idx1 = (p1.X + ((p1.X > 0)? -1 : -lineSpacing)) / lineSpacing; // -linespacing because a line between scanline -n and -n-1 belongs to scansegment -n-1 (for n=positive natural number)
            int direction = 1;
            if (p0.X > p1.X) 
            { 
                direction = -1; 
                scanline_idx1 += 1; // only consider the scanlines in between the scansegments
            } else scanline_idx0 += 1; // only consider the scanlines in between the scansegments
            
            
            if (isFirstBoundarySegment) firstBoundarySegment.push_back(p0);
            for(int scanline_idx = scanline_idx0; scanline_idx != scanline_idx1+direction; scanline_idx+=direction)
            {
                int x = scanline_idx * lineSpacing;
                int y = p1.Y + (p0.Y - p1.Y) * (x - p1.X) / (p0.X - p1.X);
                cutList[scanline_idx - scanline_min_idx].push_back(y);
                
                
                bool last_isEvenScanSegment = isEvenScanSegment;
                if (scanline_idx % 2 == 0) isEvenScanSegment = true;
                else isEvenScanSegment = false;
                
                if (!isFirstBoundarySegment)
                {
                    if (last_isEvenScanSegment && (connect_zigzags || !isEvenScanSegment))
                        addLine(lastPoint, Point(x,y));
                    else if (connect_zigzags && !last_isEvenScanSegment && !isEvenScanSegment) // if we end an uneven boundary in an uneven segment
                    { // add whole unevenBoundarySegment (including the just obtained point)
                        for (int p = 1; p < unevenBoundarySegment.size(); p++)
                        {
                            addLine(unevenBoundarySegment[p-1], unevenBoundarySegment[p]);
                        }
                        addLine(unevenBoundarySegment[unevenBoundarySegment.size()-1], Point(x,y));
                        unevenBoundarySegment.clear();
                    } 
                    if (connect_zigzags && last_isEvenScanSegment && !isEvenScanSegment)
                        unevenBoundarySegment.push_back(Point(x,y));
                    else 
                        unevenBoundarySegment.clear();
                        
                }
                lastPoint = Point(x,y);
                
                if (isFirstBoundarySegment) 
                {
                    firstBoundarySegment.emplace_back(x,y);
                    firstBoundarySegmentEndsInEven = isEvenScanSegment;
                    isFirstBoundarySegment = false;
                }
                
            }
            if (!isFirstBoundarySegment)
            {
                if (isEvenScanSegment)
                    addLine(lastPoint, p1);
                else if (connect_zigzags)
                    unevenBoundarySegment.push_back(p1);
            }
            
            lastPoint = p1;
            p0 = p1;
        }
        
        if (isEvenScanSegment || isFirstBoundarySegment || connect_zigzags)
        {
            for (int i = 1; i < firstBoundarySegment.size() ; i++)
            {
                if (i < firstBoundarySegment.size() - 1 || !firstBoundarySegmentEndsInEven || connect_zigzags) // only add last element if connect_zigzags or boundary segment ends in uneven scanline
                    addLine(firstBoundarySegment[i-1], firstBoundarySegment[i]);
            }   
        }
        else if (!firstBoundarySegmentEndsInEven)
            addLine(firstBoundarySegment[firstBoundarySegment.size()-2], firstBoundarySegment[firstBoundarySegment.size()-1]);
    } 
    
    auto compare_int64_t = [](const void* a, const void* b)
    {
        int64_t n = (*(int64_t*)a) - (*(int64_t*)b);
        if (n < 0) return -1;
        if (n > 0) return 1;
        return 0;
    };
    
    int scanline_idx = 0;
    for(int64_t x = scanline_min_idx * lineSpacing; x < boundary.max.X; x += lineSpacing)
    {
        qsort(cutList[scanline_idx].data(), cutList[scanline_idx].size(), sizeof(int64_t), compare_int64_t);
        for(unsigned int i = 0; i + 1 < cutList[scanline_idx].size(); i+=2)
        {
            if (cutList[scanline_idx][i+1] - cutList[scanline_idx][i] < extrusionWidth / 5)
                continue;
            addLine(Point(x, cutList[scanline_idx][i]), Point(x, cutList[scanline_idx][i+1]));
        }
        scanline_idx += 1;
    }
}

}//namespace cura
