package com.intel.ostro.appfw;
/**
* Incrementing id utility
*
**/
class IDCounter {

    private int id;
    public IDCounter() {
        id = 0;
    }
    
    /**
    * Returns an integer. Integer is incremented between calls 
    *
    */
    public int nextID() {
        return id++;
    }
}