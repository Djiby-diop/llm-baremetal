// src/soma/mod.rs

// "Soma" (The Body)
// This module encapsulates the "High-Level Logic" or "Useful Work" of the OS.
// In this case, it is a simplified Neural Interface simulation to demonstrate
// safe execution under the Memory Warden's protection.

// Our goal here is NOT to implement a full LLM (yet), but to implement the *shape*
// and *memory access patterns* of an LLM: sequential matrix operations,
// large buffer allocations, and iterative token generation.

use crate::warden::MemoryWarden;
use crate::types::{MemIntent, Rights, CapHandle, Access};

pub mod math;
pub mod tokenizer;

pub struct NeuralSoma {
    // Represents "Weights" loaded into memory
    weights_handle: Option<CapHandle>,
    // Represents "State" (KV Cache, Activations)
    state_handle: Option<CapHandle>,
    
    // Config
    dim: usize,
    layers_allocated: usize,
    layers_active: usize,
}

impl NeuralSoma {
    pub fn new(dim: usize, layers: usize) -> Self {
        Self {
            weights_handle: None,
            state_handle: None,
            dim, // Try 64 to fit 4 layers in 64KB
            layers_allocated: layers,
            layers_active: layers,
        }
    }

    pub fn set_active_layers(&mut self, layers: usize) {
        if layers == 0 {
            self.layers_active = self.layers_allocated;
        } else {
            self.layers_active = core::cmp::min(layers, self.layers_allocated);
        }
    }

    pub fn weights_handle(&self) -> Option<CapHandle> {
        self.weights_handle
    }
    
    // Allow updating state from input
    // Now accepts a string slice and hashes it into the state vector
    pub fn update_state_with_input<const W: usize, const C: usize, const S: usize>(
        &mut self,
        warden: &mut MemoryWarden<W, C, S>,
        step: usize,
        input_str: &str
    ) -> Result<(), &'static str> {
         let s_handle = self.state_handle.ok_or("No state")?;
         let (s_start, s_end) = warden.cap_range(s_handle).map_err(|_| "Invalid S Handle")?;
         
         // Simple "embedding": hash string into state vector
         // In real LLM, this looks up token embeddings.
         let mut h: u32 = 5381;
         for b in input_str.bytes() {
             h = ((h << 5).wrapping_add(h)) ^ (b as u32);
         }
         
         // Write hash as float to first few elements
         let v = (h as f32) / 1000000.0; // Normalize somewhat

         let token_offset = (step % 256) * self.dim * 4;
         let base = s_start + token_offset;
         if base + 4 > s_end {
             return Err("Input write out of bounds");
         }

         if warden.check_access(s_handle, Access::Write, base, 8).is_err() {
             return Err("Segfault writing input");
         }
         unsafe {
             *(base as *mut f32) = v;
             // Also pollute some other dimensions for fun
             if base + 4 < s_end {
                 *((base + 4) as *mut f32) = v * 0.5;
             }
         }
         Ok(())
    }

    /// Step 1: Request Memory for Weights (Long-term, Read-Only mostly)
    pub fn load_weights<const W: usize, const C: usize, const S: usize>(
        &mut self, 
        warden: &mut MemoryWarden<W, C, S>
    ) -> Result<(), &'static str> {
        // Calculate size: dim * dim * layers * sizeof(f32) approx
        let size_bytes = (self.dim * self.dim * self.layers_allocated * 4) as u64;
        
        let mut intent = MemIntent::new(1, size_bytes); // Owner=1 (System/Soma)
        intent.label = 0xAA; // "AI_WEIGHTS"
        intent.rights = Rights::R.union(Rights::W); // RW for loading
        intent.sandbox = false; // Trusted zone for weights? Or Sandbox? Let's say Normal for efficency.

        match warden.allocate(intent) {
            Ok(h) => {
                self.weights_handle = Some(h);
                // Initialize weights with a pattern (0.01) to verify math later.
                if let Ok((start, end)) = warden.cap_range(h) {
                    let len = (end - start) / 4;
                    let slice = unsafe { core::slice::from_raw_parts_mut(start as *mut f32, len) };
                    for i in 0..len {
                        slice[i] = 0.01;
                    }
                }
                Ok(())
            },
            Err(_) => Err("Failed to allocate weights"),
        }
    }

    /// Step 2: Request Memory for State (Short-term, Read-Write, Dynamic)
    pub fn init_state<const W: usize, const C: usize, const S: usize>(
        &mut self, 
        warden: &mut MemoryWarden<W, C, S>
    ) -> Result<(), &'static str> {
        let context_len = 256; // Smaller context for test
        let size_bytes = (self.dim * context_len * 4) as u64; // KV Cache size approx
        
        let mut intent = MemIntent::new(2, size_bytes); // Owner=2 (User Session)
        intent.label = 0xBB; // "AI_STATE"
        intent.rights = Rights::R.union(Rights::W);
        intent.sandbox = true; // State is dirty/user-specific -> Sandbox

        match warden.allocate(intent) {
            Ok(h) => {
                self.state_handle = Some(h);
                // Initialize input state to 1.0 so result = dot(0.01, 1.0) * dim
                if let Ok((start, end)) = warden.cap_range(h) {
                    let len = (end - start) / 4;
                    let slice = unsafe { core::slice::from_raw_parts_mut(start as *mut f32, len) };
                    for i in 0..len {
                        slice[i] = 1.0;
                    }
                }
                Ok(())
            },
            Err(_) => Err("Failed to allocate state"),
        }
    }

    /// Step 3: "Think" (Simulate Inference Loop)
    /// Now supports Multilayer Perceptron (MLP) simulation by looping 'layers' times.
    pub fn think_step<const W: usize, const C: usize, const S: usize>(
        &mut self, 
        warden: &mut MemoryWarden<W, C, S>,
        step: usize
    ) -> Result<f32, &'static str> {
        let w_handle = self.weights_handle.ok_or("No weights")?;
        let s_handle = self.state_handle.ok_or("No state")?;

        let (w_start, w_end) = warden.cap_range(w_handle).map_err(|_| "Invalid W Handle")?;
        let (s_start, _s_end) = warden.cap_range(s_handle).map_err(|_| "Invalid S Handle")?;
        
        // Use different region of state for different tokens
        let token_offset = (step % 256) * self.dim * 4;
        let mut activation = 0.0;
        
                // Loop through LAYERS (Multilayer Perceptron style)
                for l in 0..self.layers_active {
             // Calculate layer-specific weight region
               // One matrix per layer: [layers][dim][dim]
               let layer_stride = self.dim * self.dim * 4;
               let w_layer_base = w_start + l * layer_stride;
             
             if w_layer_base + layer_stride > w_end {
                  // Safety break if we run out of weight memory
                  break;
             }

             // Each step accesses a different "row" of weights - offset by step
               let row_offset = (step % self.dim) * self.dim * 4;
             
             // Verify Warden Access for Weights
             if warden.check_access(w_handle, Access::Read, w_layer_base + row_offset, self.dim * 4).is_err() {
                 return Err("Segfault reading weights (Layer)");
             }
             
             // Verify Warden Access for State
             // Note: In real MLP, we'd ping-pong between two state buffers. 
             // Here we read/write to same for simulation simplicity.
             if warden.check_access(s_handle, Access::Read, s_start + token_offset, self.dim * 4).is_err() {
                 return Err("Segfault reading state");
             }

             let w_slice = unsafe { core::slice::from_raw_parts(
                 (w_layer_base + row_offset) as *const f32, 
                 self.dim
             )};
             
             let s_slice = unsafe { core::slice::from_raw_parts(
                 (s_start + token_offset) as *const f32,
                 self.dim
             )};

             let dot = math::dot_product(w_slice, s_slice);
             activation += dot;

             // Residual Update: State = State + Dot * 0.1
             // This mimics the "Add" in "Add & Norm"
             if warden.check_access(s_handle, Access::Write, s_start + token_offset, 4).is_err() {
                return Err("Segfault updating state");
             }
             let out_ptr = (s_start + token_offset) as *mut f32;
             unsafe { *out_ptr = *out_ptr + dot * 0.1 }; 
        }
        
        Ok(activation)
    }

    /// Simulate a Hallucination (Out of bounds access)
    pub fn hallucinate<const W: usize, const C: usize, const S: usize>(
        &mut self,
        warden: &mut MemoryWarden<W, C, S>
    ) -> Result<(), &'static str> {
        let s_handle = self.state_handle.ok_or("No state")?;
        let (_s_start, s_end) = warden.cap_range(s_handle).map_err(|_| "Invalid S Handle")?;
        
        // Access WAY out of bounds
        let bad_addr = s_end + 1024; 
        if warden.check_access(s_handle, Access::Write, bad_addr, 4).is_ok() {
            return Err("Warden failed to catch hallucination!");
        }
        Ok(())
    }
}
