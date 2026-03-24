// UserStore.swift — Local photo cache for Lynrix v2.5
// Firebase Auth + Firestore are the primary account systems.
// This file now only provides local profile photo caching.

import Foundation
import os.log

private let storeLogger = Logger(subsystem: "com.lynrix.trader", category: "LocalPhotoCache")

// MARK: - Local Photo Cache

final class LocalPhotoCache {
    static let shared = LocalPhotoCache()
    
    private let fileManager = FileManager.default
    
    private var photosDir: URL {
        let appSupport = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        let dir = appSupport.appendingPathComponent("Lynrix/ProfilePhotos", isDirectory: true)
        if !fileManager.fileExists(atPath: dir.path) {
            try? fileManager.createDirectory(at: dir, withIntermediateDirectories: true)
        }
        return dir
    }
    
    private init() {}
    
    // MARK: - Photo Storage
    
    func savePhoto(_ data: Data, forUserId userId: String) -> String? {
        let filename = "\(userId).jpg"
        let url = photosDir.appendingPathComponent(filename)
        do {
            try data.write(to: url)
            storeLogger.info("Saved profile photo for \(userId)")
            return url.path
        } catch {
            storeLogger.error("Failed to save profile photo: \(error.localizedDescription)")
            return nil
        }
    }
    
    func loadPhoto(forUserId userId: String) -> Data? {
        let filename = "\(userId).jpg"
        let url = photosDir.appendingPathComponent(filename)
        guard fileManager.fileExists(atPath: url.path) else {
            return nil
        }
        return try? Data(contentsOf: url)
    }
    
    func photoPath(forUserId userId: String) -> String? {
        let filename = "\(userId).jpg"
        let url = photosDir.appendingPathComponent(filename)
        return fileManager.fileExists(atPath: url.path) ? url.path : nil
    }
    
    func deletePhoto(forUserId userId: String) {
        let filename = "\(userId).jpg"
        let url = photosDir.appendingPathComponent(filename)
        try? fileManager.removeItem(at: url)
    }
}
