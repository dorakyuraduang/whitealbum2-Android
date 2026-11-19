package org.wa2.port;

/** Counts successful presents on one uninterrupted foreground interval. */
final class TextWorkWindows {
    static final class Window {
        final long segment, frames, glyphs, tiles, wallNs, cpuNs, replayedGlyphs, reusedGeometry;
        final long rollovers, avoidedDriverCalls;
        final long particleCommands, particleTiles, particleContinuations;
        Window(long segment, long frames, long glyphs, long tiles, long wallNs, long cpuNs,
               long replayedGlyphs, long reusedGeometry, long rollovers, long avoidedDriverCalls,
               long particleCommands, long particleTiles, long particleContinuations) {
            this.segment=segment; this.frames=frames; this.glyphs=glyphs; this.tiles=tiles;
            this.wallNs=wallNs; this.cpuNs=cpuNs; this.replayedGlyphs=replayedGlyphs; this.reusedGeometry=reusedGeometry;
            this.rollovers=rollovers; this.avoidedDriverCalls=avoidedDriverCalls;
            this.particleCommands=particleCommands; this.particleTiles=particleTiles;
            this.particleContinuations=particleContinuations;
        }
        int group() {
            double load=(double)glyphs/frames;
            return load >= 1 && load <= 128 ? 1 : load >= 256 ? 2 : 0;
        }
        boolean resembles(Window other) {
            if (other == null || group() != other.group()) return false;
            return similar((double)glyphs/frames,(double)other.glyphs/other.frames)
                    && similar((double)tiles/frames,(double)other.tiles/other.frames);
        }
        private static boolean similar(double x,double y) {
            return Math.abs(x-y) <= Math.max(1,Math.min(x,y)*0.02);
        }
        boolean sameParticles(Window other) {
            return other!=null && particleCommands>0 && other.particleCommands>0
                    && similarParticles((double)particleCommands/frames,(double)other.particleCommands/other.frames)
                    && similarParticles((double)particleTiles/frames,(double)other.particleTiles/other.frames);
        }
        private static boolean similarParticles(double x,double y) {
            return Math.abs(x-y)<=Math.max(0.25,Math.min(x,y)*0.1);
        }
        Window plus(Window other) {
            return new Window(segment,frames+other.frames,glyphs+other.glyphs,tiles+other.tiles,
                    wallNs+other.wallNs,cpuNs<0 || other.cpuNs<0 ? -1 : cpuNs+other.cpuNs,
                    replayedGlyphs+other.replayedGlyphs,reusedGeometry+other.reusedGeometry,
                    rollovers+other.rollovers,avoidedDriverCalls+other.avoidedDriverCalls,
                    particleCommands+other.particleCommands,particleTiles+other.particleTiles,
                    particleContinuations+other.particleContinuations);
        }
    }
    private boolean active, baseline, changed, production=true;
    private long segment, frames, glyphs, tiles, wallNs, cpuNs, replayedGlyphs, reusedGeometry;
    private long rollovers, avoidedDriverCalls;
    private long particleCommands,particleTiles,particleContinuations;
    private Window older, previous, light, heavy, particles;

    synchronized void reset() {
        active=false; baseline=false; changed=false; production=true; segment=0;
        older=null; previous=null; light=null; heavy=null; particles=null;
    }
    synchronized void foreground(boolean value) {
        if (value==active) return;
        active=value;
        if (value) ++segment;
        baseline=false; older=null; previous=null;
    }
    synchronized void modeChanged(boolean optimized) {
        production=optimized;
        ++segment;
        baseline=false; older=null; previous=null;
    }
    synchronized Window observe(long endFrames,long endGlyphs,long endTiles,long endWall,long endCpu,
                                long endReplayedGlyphs,long endReusedGeometry,long endRollovers,long endAvoidedDriverCalls,
                                long endParticleCommands,long endParticleTiles,long endParticleContinuations) {
        if (!active) return null;
        Window now=null;
        long elapsed=endWall-wallNs;
        if (baseline && endFrames>frames && endGlyphs>=glyphs && endTiles>=tiles && endReplayedGlyphs>=replayedGlyphs && endReusedGeometry>=reusedGeometry
                && endRollovers>=rollovers && endAvoidedDriverCalls>=avoidedDriverCalls
                && endParticleCommands>=particleCommands && endParticleTiles>=particleTiles
                && endParticleContinuations>=particleContinuations
                && elapsed>=200_000_000L && elapsed<=750_000_000L) {
            long cpu=cpuNs>=0 && endCpu>=cpuNs ? endCpu-cpuNs : -1;
            now=new Window(segment,endFrames-frames,endGlyphs-glyphs,endTiles-tiles,elapsed,cpu,
                    endReplayedGlyphs-replayedGlyphs,endReusedGeometry-reusedGeometry,
                    endRollovers-rollovers,endAvoidedDriverCalls-avoidedDriverCalls,
                    endParticleCommands-particleCommands,endParticleTiles-particleTiles,endParticleContinuations-particleContinuations);
        }
        if (baseline && now==null) ++segment;
        baseline=true; frames=endFrames; glyphs=endGlyphs; tiles=endTiles; wallNs=endWall; cpuNs=endCpu;
        replayedGlyphs=endReplayedGlyphs; reusedGeometry=endReusedGeometry;
        rollovers=endRollovers; avoidedDriverCalls=endAvoidedDriverCalls;
        particleCommands=endParticleCommands; particleTiles=endParticleTiles; particleContinuations=endParticleContinuations;
        if (now==null) { older=null; previous=null; return null; }
        if (production && now.group()!=0 && now.resembles(older) && now.resembles(previous) && older.resembles(previous)) {
            Window stable=older.plus(previous).plus(now);
            if (now.group()==1) light=stable; else heavy=stable;
            changed=true;
        }
        if(production && now.sameParticles(older) && now.sameParticles(previous) && older.sameParticles(previous)) {
            particles=older.plus(previous).plus(now);
            changed=true;
        }
        older=previous; previous=now;
        return now;
    }
    // The activity's pause/detach callbacks publish at most one new summary.
    synchronized Window[] takeSummary() {
        if (!changed) return null;
        changed=false;
        return new Window[]{light,heavy,particles};
    }
}
