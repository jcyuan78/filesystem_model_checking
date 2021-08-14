#ifndef CLASS_LOADER_H
#define CLASS_LOADER_H

//#include <dlfcn.h>
#include <crashmonkey_comm.h>
#include <iostream>
#include <map>

#define SUCCESS             0
#define CASE_HANDLE_ERR     -1
#define CASE_INIT_ERR       -2
#define CASE_DEST_ERR       -3



namespace fs_testing 
{
    namespace utils 
    {
        //<yuan> 在windows下做以下改进：
        // (1) 将factory函数改成factory类，
        // (2) GetTesterFactory函数用于加载静态库或者exe中的类。
        extern "C" bool GetTesterFactory(ITesterFactory * &fac, const wchar_t* name);

        template<class T> class ClassLoader
        {
        //protected:
        //    typedef bool(*GET_FACTORY)(ITesterFactory * & f, const wchar_t * name);

        public:
            ClassLoader<T>(void) : m_factory(NULL) {};
            ~ClassLoader<T>(void) { RELEASE(m_factory); }

        public:
            T *get_instance() 
            {
                return instance;
            }

  // TODO(ashmrtn): Make this load the handles only. Since each constructor can
  // be different, make just a method that returns the factory and have the user
  // provide the instance on unload. The other option is to restrict all default
  // loaded classes to having a default constructor. Then the class must also
  // provide a method to load data if needed, but that seems like the cleanest
  // solution here...
            template<typename F> 
            int load_class(const wchar_t *module_name, const wchar_t *class_name/*, const wchar_t *defactory_name*/) 
            {
                GET_FACTORY get_factory = NULL;
                //const wchar_t* dl_error = NULL;
                if (module_name == nullptr || module_name[0] ==0)     get_factory = GetTesterFactory;
                else
                {
                    //if (lib_name.empty())	THROW_ERROR(ERR_PARAMETER, L"missing DLL.");
                    LOG_DEBUG(L"loading dll: %s...", module_name);
                    HMODULE plugin = LoadLibrary(module_name);
                    if (plugin == NULL) THROW_WIN32_ERROR(L" failure on loading driver %s ", module_name);

                    LOG_DEBUG(L"getting entry...");
                    get_factory = (GET_FACTORY)(GetProcAddress(plugin, "GetTesterFactory"));
                    if (get_factory == NULL)	THROW_WIN32_ERROR(L"file %s is not a file system plugin.", module_name);
                }
                JCASSERT(get_factory);
                    //    jcvos::auto_interface<IFsFactory> factory;
                jcvos::auto_interface<ITesterFactory> ff;
                bool br = (get_factory)(ff, class_name);
                if (!br || !ff) THROW_ERROR(ERR_USER, L"failed on getting plugin register in %s", module_name);
                ff.detach(m_factory);
                if (m_factory == NULL) THROW_ERROR(ERR_APP, L"factroy type does not match");


                    //loader_handle = dlopen(path, RTLD_LAZY);
                    //if (loader_handle == NULL) 
                    //{
                    //    std::cerr << "Error loading class " << path << std::endl << dlerror() << std::endl;
                    //    return CASE_HANDLE_ERR;
                    //}

                    // Get needed methods from loaded class.
                    //factory = dlsym(loader_handle, factory_name);
                    //dl_error = dlerror();
                    //if (dl_error)
                    //{
                    //    std::cerr << "Error gettig factory method " << dl_error << std::endl;
                    //    dlclose(loader_handle);
                    //    factory = NULL;
                    //    defactory = NULL;
                    //    loader_handle = NULL;
                    //    return CASE_INIT_ERR;
                    //}

                    //defactory = dlsym(loader_handle, defactory_name);
                    //dl_error = dlerror();
                    //if (dl_error)
                    //{
                    //    std::cerr << "Error gettig deleter method " << dl_error << std::endl;
                    //    dlclose(loader_handle);
                    //    factory = NULL;
                    //    defactory = NULL;
                    //    loader_handle = NULL;
                    //    return CASE_DEST_ERR;
                    //}
//                instance = ((F)(factory))();
                m_factory->CreateObject(instance);
                return SUCCESS;
            };

            template<typename DF>
            void unload_class() 
            {
                //if (loader_handle != NULL && instance != NULL) 
                //{
                //    ((DF)(defactory))(instance);
                //    //if (loader_handle) dlclose(loader_handle);
                //    factory = NULL;
                //    defactory = NULL;
                //    loader_handle = NULL;
                //    instance = NULL;
                //}
                m_factory->DeleteObject(instance);
            };

        private:
            typename T::Factory* m_factory = NULL;
            //void *loader_handle = NULL;
            //void *factory = NULL;
            //void *defactory = NULL;
            T *instance = NULL;
        };
    }  // namespace utils
}  // namespace fs_testing
#endif
